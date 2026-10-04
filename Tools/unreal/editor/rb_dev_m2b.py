"""M2-B dev pipeline: imports the prop exports, checks them (VDB-T4 / VDB-T7) and builds the look-dev slice of The Low Bridge Tavern
(Docs/ue-architecture.md 18.8; venue-dive-bar 2.3, 4.2, 12.2). Runs INSIDE Unreal:

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2b.py                    # import + checks + dev level
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2b.py -- --import-only
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2b.py -- --level-only
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2b.py -- --only CueRack Sign_CashOnly   # re-import some, then the level
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2b.py -- --ledger-negative   # DB-0: the ledger check must refuse once

Import (the M2-B side of the pipeline; M2-A's rb_import_divebar.py does the same for the level and may reuse import_asset()):
  every Art/DiveBar/Export/Props/<Asset>/<Asset>.json -> Interchange FBX import (materials off, UCX collision, Nanite unless the
  asset is translucent) to its ue_dir (/Game/Generated/Venues/DiveBar/Props/<Family>/SM_DB_<Asset>); refused without a ledger row
  (VDB-T7: Docs/licenses/ledger/M2-B.csv + asset-ledger.csv; no NC licence); material slots -> MI_DB_* through
  rb_make_divebar_materials.asset_materials() (per-asset wear-mask children); bounds re-checked against the Blender metadata
  (VDB-T4, hero +-2 mm / others +-1 cm; Blender (x, y, z) m -> Unreal (x, -y, z) cm).
Dev level /Game/Dev/M2B/L_M2B_LowBridge (git-ignored): a greybox shell of the main room at the venue frame (venue-dive-bar 2.2)
dressed with the venue materials (WorldUV boxes), every M2-B prop at its E-table position, the back bar's prop instances, the
wall signs at their placement hints, the first decal set as projected DBuffer decals (floor chalk / heel marks / spills, bar-die
kicks, the S10 dings band, rack chalk prints, ceiling leaks; tagged RbDB_Decal), the coin-op table (ARbTable SevenFootBar /
OldBarOversizedCue, M1 content) under the 3-shade lamp, the night light rig of 4.2 (lamp bulbs, bar pendants, back-bar strips,
coolers, sconces, jukebox / dart glow, neon proxies), height fog, and ARbLookDevCamera views tagged M2B_* (per-prop look-dev + the
DB-3 pre-check views V01 / V02 / V03 / V04 / V05 / V09). The real L_DiveBar is M2-A's.
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_dev_m2b.py -- --list      # the capture command of every M2B_* camera
Owner: M2-B.
"""

from __future__ import annotations

import csv
import json
import math
import os
import random
import sys

import unreal

EDITOR_DIR = os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor")
sys.path.insert(0, EDITOR_DIR)
import rb_common as rb  # noqa: E402
import rb_make_divebar_materials as mats  # noqa: E402

PROJECT = os.path.abspath(unreal.Paths.project_dir())
EXPORT = os.path.join(PROJECT, "Art", "DiveBar", "Export", "Props")
LEDGERS = [os.path.join(PROJECT, "Docs", "licenses", "ledger", "M2-B.csv"), os.path.join(PROJECT, "Docs", "licenses", "asset-ledger.csv")]
DEV = "/Game/Dev/M2B"
MAP = DEV + "/L_M2B_LowBridge"
MAT = mats.MAT_DIR
NC_LICENCES = ("NC", "NonCommercial", "Non-Commercial")
TABLE_LOC = (1375.9, 542.7, 0.0)
MATCH_OPTIONS = "?Mode=Practice?Game=EightBall?Seed=3?Rate=0"


# --------------------------------------------------------------------------------------------------------------------
# ledger (VDB-T7) and import (VDB-T4)
# --------------------------------------------------------------------------------------------------------------------


def ledger_rows(paths=None) -> dict:
	rows = {}
	for path in paths or LEDGERS:
		if not os.path.exists(path):
			continue
		with open(path, newline="", encoding="utf-8") as handle:
			for row in csv.DictReader(handle):
				rows.setdefault(row["asset_id"], []).append(row)
	return rows


def ledger_check(meta: dict, rows: dict) -> list:
	"""Problems of one asset: a missing row for the mesh or an external input, or a non-commercial licence."""
	problems = []
	for asset_id in [meta["ledger_asset_id"]] + [str(x) for x in meta.get("external_inputs", [])]:
		found = rows.get(asset_id)
		if not found:
			problems.append(f"no ledger row for {asset_id}")
			continue
		for row in found:
			if any(tag.lower() in row.get("licence", "").lower() for tag in NC_LICENCES):
				problems.append(f"{asset_id}: non-commercial licence {row['licence']}")
	return problems


def asset_jsons() -> list:
	out = []
	for name in sorted(os.listdir(EXPORT)):
		path = os.path.join(EXPORT, name, name + ".json")
		if os.path.isfile(path):
			out.append(path)
	return out


def interchange_import(fbx: str, dest: str, nanite: bool) -> None:
	pipeline = unreal.InterchangeGenericAssetsPipeline()
	pipeline.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
	mesh = pipeline.get_editor_property("mesh_pipeline")
	mesh.set_editor_property("import_static_meshes", True)
	mesh.set_editor_property("import_skeletal_meshes", False)
	mesh.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
	mesh.set_editor_property("collision", True)
	mesh.set_editor_property("import_collision_according_to_mesh_name", True)
	mesh.set_editor_property("one_convex_hull_per_ucx", True)
	mesh.set_editor_property("build_nanite", nanite)
	common = pipeline.get_editor_property("common_meshes_properties")
	common.set_editor_property("recompute_normals", False)
	common.set_editor_property("recompute_tangents", True)
	pipeline.set_editor_property("use_source_name_for_asset", True)
	source = unreal.InterchangeManager.create_source_data(fbx)
	params = unreal.ImportAssetParameters()
	params.set_editor_property("is_automated", True)
	params.set_editor_property("replace_existing", True)
	params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
	manager = unreal.InterchangeManager.get_interchange_manager_scripted()
	manager.import_asset(dest, source, params)
	manager.wait_until_all_tasks_done(False)  # False: wait for queued work, do not cancel it (as M2-A's importer)


def import_asset(json_path: str, rows: dict) -> dict:
	with open(json_path, "r", encoding="utf-8") as handle:
		meta = json.load(handle)
	asset_id = meta["asset_id"]
	problems = ledger_check(meta, rows)
	if problems:
		rb.fail(f"{asset_id} refused (VDB-T7): {'; '.join(problems)}")
	dest = meta["ue_dir"]
	path = f"{dest}/SM_DB_{asset_id}"
	fbx = os.path.join(os.path.dirname(json_path), meta["fbx"])
	interchange_import(fbx, dest, bool(meta.get("nanite", True)))
	mesh = unreal.load_asset(path)
	if not isinstance(mesh, unreal.StaticMesh):
		rb.fail(f"{asset_id}: import produced no static mesh at {path}")
	# full-detail Nanite fallback like M2-A's importer (ARCH 2.2): ray-traced views (seen through glass, reflections) and translucent
	# sections use the fallback mesh; the default fallback collapses small parts (the cooler cans) to prisms
	nanite_settings = mesh.get_editor_property("nanite_settings")
	if nanite_settings.get_editor_property("enabled"):
		# fallback_target first: with the default (Auto) UE 5.8 ignores the percent / relative-error values
		for key, value in (("fallback_target", getattr(unreal.NaniteFallbackTarget, "PERCENT_TRIANGLES", None)),
				("fallback_percent_triangles", 1.0), ("fallback_relative_error", 0.0)):
			if value is None:
				continue
			try:
				nanite_settings.set_editor_property(key, value)
			except Exception:  # noqa: BLE001 - property names differ between engine versions
				pass
		mesh.set_editor_property("nanite_settings", nanite_settings)
	# materials by slot name (MI_DB_* / per-asset wear-mask children)
	slot_mats = mats.asset_materials(json_path)
	static = mesh.get_editor_property("static_materials")
	for i, sm in enumerate(static):
		name = str(sm.get_editor_property("material_slot_name"))
		if name not in slot_mats:
			rb.fail(f"{asset_id}: slot {name} has no material")
		mesh.set_material(i, unreal.load_asset(slot_mats[name]))
	# VDB-T4: Unreal bounds vs the Blender metadata (render mesh; hulls are separate bodies)
	box = mesh.get_bounding_box()
	lo_b, hi_b = meta["bounds_min_m"], meta["bounds_max_m"]
	want_lo = (lo_b[0] * 100.0, -hi_b[1] * 100.0, lo_b[2] * 100.0)
	want_hi = (hi_b[0] * 100.0, -lo_b[1] * 100.0, hi_b[2] * 100.0)
	tol = meta.get("tolerance_m", 0.01) * 100.0
	have_lo = (box.min.x, box.min.y, box.min.z)
	have_hi = (box.max.x, box.max.y, box.max.z)
	for k in range(3):
		if abs(have_lo[k] - want_lo[k]) > tol or abs(have_hi[k] - want_hi[k]) > tol:
			rb.fail(f"{asset_id}: VDB-T4 bounds {have_lo} .. {have_hi} cm vs Blender {want_lo} .. {want_hi} (+-{tol} cm)")
	body = mesh.get_editor_property("body_setup")
	hulls = len(body.get_editor_property("agg_geom").get_editor_property("convex_elems")) if body else 0
	if meta.get("collision_hulls", 0) and hulls == 0:
		rb.fail(f"{asset_id}: no UCX collision imported")
	unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
	rb.log(f"import {asset_id}: bounds ok ({have_lo[0]:.1f},{have_lo[1]:.1f},{have_lo[2]:.1f})..({have_hi[0]:.1f},{have_hi[1]:.1f},{have_hi[2]:.1f}) cm, "
		f"{len(static)} slots, {hulls} hulls, nanite {meta.get('nanite', True)}")
	return meta


def import_all(only=None) -> dict:
	"""Imports every exported prop (or the asset ids in only)."""
	rows = ledger_rows()
	metas = {}
	for path in asset_jsons():
		if only and os.path.basename(os.path.dirname(path)) not in only:
			continue
		meta = import_asset(path, rows)
		metas[meta["asset_id"]] = meta
	if only and len(metas) != len(set(only)):
		rb.fail(f"--only: no export for {sorted(set(only) - set(metas))}")
	rb.log(f"imported {len(metas)} props (VDB-T4 bounds and VDB-T7 ledger checks passed)")
	return metas


def ledger_negative() -> None:
	"""DB-0: the ledger check fails on purpose once - an asset whose ledger row is missing must be refused."""
	meta = {"asset_id": "LedgerNegativeTest", "ledger_asset_id": "SM_DB_LedgerNegativeTest", "external_inputs": ["no_such_cc0_input"]}
	problems = ledger_check(meta, ledger_rows())
	if len(problems) != 2:
		rb.fail(f"ledger negative test: expected 2 refusals, got {problems}")
	nc = ledger_check({"asset_id": "x", "ledger_asset_id": "x", "external_inputs": []}, {"x": [{"licence": "CC-BY-NC-4.0"}]})
	if not nc:
		rb.fail("ledger negative test: a non-commercial licence was accepted")
	rb.log(f"ledger negative test OK: refused {problems + nc}")


# --------------------------------------------------------------------------------------------------------------------
# dev level
# --------------------------------------------------------------------------------------------------------------------


def dev_mi(base: str, suffix: str, params: dict) -> str:
	"""A child of a shared MI_DB_* with dev overrides (WorldUV for engine cubes)."""
	path = f"{DEV}/Materials/{base}_{suffix}"
	mi = mats._mi(path, f"{MAT}/{base}")
	for key, value in params.items():
		if isinstance(value, (tuple, list)):
			mats.MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(value[0], value[1], value[2], 1.0))
		else:
			mats.MEL.set_material_instance_scalar_parameter_value(mi, key, float(value))
	mats.MEL.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	return path


def box(label: str, lo, hi, material: str, shadow: bool = True, tags=None):
	c = [(a + b) / 2.0 for a, b in zip(lo, hi)]
	s = [max(0.1, abs(b - a)) / 100.0 for a, b in zip(lo, hi)]
	actor = rb.spawn_mesh("/Engine/BasicShapes/Cube.Cube", c, scale=s, label=label, material_path=material)
	actor.static_mesh_component.set_editor_property("cast_shadow", shadow)
	if tags:
		actor.tags = tags
	return actor


def cylinder(label: str, center_xy, z0: float, z1: float, radius: float, material: str):
	actor = rb.spawn_mesh("/Engine/BasicShapes/Cylinder.Cylinder", (center_xy[0], center_xy[1], (z0 + z1) / 2.0),
		scale=(radius / 50.0, radius / 50.0, (z1 - z0) / 100.0), label=label, material_path=material)
	return actor


def prop(asset_id: str, loc, yaw: float = 0.0, label: str | None = None, profile: str | None = None, material_override=None,
		shadow=None, ray_tracing=None, metas=None, pitch: float = 0.0, roll: float = 0.0):
	meta = (metas or {}).get(asset_id, {})
	path = meta.get("ue_mesh") or f"/Game/Generated/Venues/DiveBar/Props/{meta.get('family', '')}/SM_DB_{asset_id}"
	actor = rb.spawn_mesh(path, loc, rotation=(pitch, yaw, roll), label=label or asset_id)
	comp = actor.static_mesh_component
	comp.set_collision_profile_name(profile or meta.get("collision_profile", "RbVenueBlock"))
	if shadow is not None or meta.get("cast_shadow") is False:
		comp.set_editor_property("cast_shadow", bool(shadow) if shadow is not None else False)
	if ray_tracing is not None or meta.get("ray_tracing") is False:
		comp.set_editor_property("visible_in_ray_tracing", bool(ray_tracing) if ray_tracing is not None else False)
	if material_override:
		for index, mat_path in material_override.items():
			comp.set_material(index, unreal.load_asset(mat_path))
	return actor


def local_to_world(origin, yaw_deg: float, p_cm):
	a = math.radians(yaw_deg)
	x, y, z = p_cm
	return (origin[0] + x * math.cos(a) - y * math.sin(a), origin[1] + x * math.sin(a) + y * math.cos(a), origin[2] + z)


def light(kind, label: str, loc, rot=(0.0, 0.0, 0.0), lumens: float = 100.0, kelvin: float = 2700.0, radius: float = 2.0,
		length: float = 0.0, width: float = 0.0, height: float = 0.0, shadows: bool = True, scattering: float = 0.0, specular: float = 1.0,
		color=None, attenuation: float = 1500.0):
	actor = rb.spawn(kind, loc, rot, label)
	lc = actor.light_component
	lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	lc.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
	lc.set_editor_property("intensity", float(lumens))
	if color is None:
		lc.set_editor_property("use_temperature", True)
		lc.set_editor_property("temperature", float(kelvin))
	else:
		lc.set_editor_property("light_color", unreal.Color(int(color[0] * 255), int(color[1] * 255), int(color[2] * 255), 255))
	lc.set_editor_property("cast_shadows", shadows)
	lc.set_editor_property("volumetric_scattering_intensity", float(scattering))
	if scattering > 0.0:
		lc.set_editor_property("cast_volumetric_shadow", True)
	lc.set_editor_property("specular_scale", float(specular))
	lc.set_editor_property("attenuation_radius", float(attenuation))
	if kind is unreal.RectLight:
		lc.set_editor_property("source_width", float(width))
		lc.set_editor_property("source_height", float(height))
		lc.set_editor_property("barn_door_length", 0.0)
	else:
		lc.set_editor_property("source_radius", float(radius))
		lc.set_editor_property("source_length", float(length))
	return actor


def shell(dm: dict) -> None:
	"""Greybox of the main room (venue-dive-bar 2.2), dressed with the venue instances; the real shell is M2-A's db_arch.py."""
	L, W, Hc = 1646.0, 732.0, 274.0
	floor = f"{MAT}/MI_DB_VCT_Oxblood"
	box("Floor", (-40, -40, -10), (1700, 780, 0), floor)
	# left wall (Y = 0): behind the bar cream plaster over brick; the pool room paneling to 1.22 m, oxblood plaster above
	box("WallL_Bar", (-40, -40, 0), (950, 0, Hc), dm["plaster_cream"])
	box("WallL_PoolPanel", (950, -40, 0), (1646, 0, 122), dm["paneling"])
	box("WallL_Pool", (950, -40, 122), (1646, 0, Hc), dm["plaster_ox"])
	box("WallR_Bar", (-40, W, 0), (950, W + 40, Hc), dm["brick"])
	box("WallR_PoolPanel", (950, W, 0), (1646, W + 40, 122), dm["paneling"])
	box("WallR_Pool", (950, W, 122), (1646, W + 40, Hc), dm["plaster_ox"])
	# back wall (block) with the corridor opening E18 and the storage door E19
	box("WallBack_A", (L, -40, 0), (L + 20, 30, Hc), dm["block"])
	box("WallBack_B", (L, 140, 0), (L + 20, 455, Hc), dm["block"])
	box("WallBack_C", (L, 546, 0), (L + 20, W + 40, Hc), dm["block"])
	box("WallBack_Lintel1", (L, 30, 213), (L + 20, 140, Hc), dm["block"])
	box("WallBack_Lintel2", (L, 455, 213), (L + 20, 546, Hc), dm["block"])
	box("StorageDoor", (L + 2, 456, 0), (L + 6, 545, 212), dm["door"])
	box("CorridorVoid", (L + 20, 30, 0), (L + 200, 140, 244), f"{MAT}/MI_DB_Block_Painted")
	box("WallFront", (-40, -40, 0), (0, W + 40, Hc), dm["brick"])
	# ceiling: tiles + T-bar grid; the beam soffit on the column line
	box("Ceiling", (-40, -40, Hc), (1700, 780, Hc + 10), dm["ceiling"], tags=["RbDB_Ceiling"])
	grid = f"{MAT}/MI_DB_Enamel_WhiteInt"
	y = 0.0
	while y <= W:
		box(f"TBarY_{int(y)}", (0, y - 1.2, Hc - 1.0), (L, y + 1.2, Hc), grid, shadow=False, tags=["RbDB_Ceiling"])
		y += 61.0
	x = 0.0
	while x <= L:
		box(f"TBarX_{int(x)}", (x - 1.2, 0, Hc - 1.0), (x + 1.2, W, Hc), grid, shadow=False, tags=["RbDB_Ceiling"])
		x += 122.0
	box("Soffit", (0, 348, 244), (L, 384, Hc), dm["ceiling"], tags=["RbDB_Ceiling"])
	for k, cx in enumerate((457.0, 914.0, 1372.0)):
		cylinder(f"Column_C{k + 1}", (cx, 366.0), 0.0, 244.0, 5.7, dm["blacksteel"])


def props(metas: dict, rng: random.Random) -> None:
	prop("BarCounter", (579.0, 199.5, 0.0), 90.0, metas=metas)
	bb_origin, bb_yaw = (564.0, 0.0, 0.0), 90.0
	prop("BackBar", bb_origin, bb_yaw, metas=metas)
	for i, inst in enumerate(metas.get("BackBar", {}).get("prop_instances", [])):
		asset = inst["asset"].replace("SM_DB_", "")
		loc = local_to_world(bb_origin, bb_yaw, [c * 100.0 for c in inst["location_ue_m"]])
		prop(asset, loc, bb_yaw + inst["yaw_deg"], label=f"BB_{i:03d}_{asset}", profile="RbVenueProp", metas=metas)
	# bar stools #1 - #10 (E07): X 2.30 + 0.70 i, Y 2.62; +-3 cm, random yaw (9.2)
	variants = ["A", "C", "A", "B", "A", "A", "C", "B", "A", "A"]
	for i in range(10):
		x = 230.0 + 70.0 * i + rng.uniform(-3, 3)
		y = 262.0 + rng.uniform(-3, 3)
		prop(f"BarStool_{variants[i]}", (x, y, 0.0), rng.uniform(0, 360), label=f"Stool_{i + 1:02d}", metas=metas)
	for k, x in enumerate((1120.0, 1260.0, 1400.0)):
		prop("SpectatorStool", (x + rng.uniform(-3, 3), 55.0 + rng.uniform(-3, 3), 0.0), rng.uniform(0, 360), label=f"SpecStool_{k}", metas=metas)
	prop("LedgeLeftWall", (1310.0, 0.0, 0.0), 90.0, metas=metas)
	prop("LedgeBack", (1646.0, 635.0, 0.0), 180.0, metas=metas)
	prop("ColumnShelf", (1372.0, 366.0, 0.0), 90.0, metas=metas)
	prop("CueRack", (1380.0, 732.0, 0.0), -90.0, metas=metas)
	prop("Jukebox", (1130.0, 732.0, 0.0), -90.0 + 1.2, metas=metas)
	prop("DartMachine", (890.0, 732.0, 0.0), -90.0 - 0.8, metas=metas)
	# wall signs and paper (db_signs.py): at their placement hints, a little crooked (9.2: 1 in 10 visibly, the rest 0.5 - 2.5 deg)
	for asset_id, meta in sorted(metas.items()):
		hint = meta.get("placement_hint_ue_cm")
		if meta.get("family") != "Signs" or not hint:
			continue
		# taped paper hangs crooked (the band flyer visibly), screwed / bolted plates stay put; the roll turns about the floor-level pivot
		tilt = rng.uniform(0.5, 2.5) * rng.choice((-1.0, 1.0)) * (2.2 if asset_id == "Flyer_Band" else 1.0) if meta.get("kind") in ("paper", "cardboard") else 0.0
		prop(asset_id, tuple(hint["location"]), hint["yaw_deg"], profile="RbVenueProp", metas=metas, roll=tilt)
	# booths (E08): end benches at X 2.13 / 7.92, doubles at 4.06 / 5.99, tables at the set centres
	prop("BoothBench_End", (213.0 + 29.25, 678.5, 0.0), 0.0, label="Booth_End_A", metas=metas)
	prop("BoothBench_Double", (406.0, 678.5, 0.0), 0.0, label="Booth_Double_A", metas=metas)
	prop("BoothBench_Double", (599.0, 678.5, 0.0), 180.0, label="Booth_Double_B", metas=metas)
	prop("BoothBench_End", (792.0 - 29.25, 678.5, 0.0), 180.0, label="Booth_End_B", metas=metas)
	for k, x in enumerate((309.5, 502.5, 695.5)):
		prop("BoothTable", (x + rng.uniform(-2, 2), 678.5 + rng.uniform(-2, 2), 0.0), rng.uniform(-2, 2), label=f"BoothTable_{k}", metas=metas)
	# the lamp over the table (E14: +2 cm Y, 1.5 deg yaw), its bulbs at the anchors (no shadow, not in ray tracing: 4.1)
	lamp_o, lamp_yaw = (TABLE_LOC[0], TABLE_LOC[1] + 2.0, 274.0), 1.5
	prop("TableLamp_OldCastor", lamp_o, lamp_yaw, metas=metas)
	anchors = metas.get("TableLamp_OldCastor", {}).get("anchors_ue_m", {})
	for k in range(3):
		a = anchors.get(f"bulb_L{k + 1}", [(-0.46 + 0.46 * k), 0.0, -0.987])
		loc = local_to_world(lamp_o, lamp_yaw, [c * 100.0 for c in a])
		over = {0: f"{MAT}/MI_DB_Emissive_Bulb3000"} if k == 2 else None
		prop("LampBulb_A19", loc, lamp_yaw, label=f"LampBulb_L{k + 1}", material_override=over, shadow=False, ray_tracing=False, metas=metas)
		light(unreal.PointLight, f"LT_DB_L{k + 1}", loc, lumens=1100.0, kelvin=3000.0 if k == 2 else 2700.0, radius=3.0, scattering=1.0,
			attenuation=900.0)
	# L5-L8 bar mini pendants: the cone body at its ceiling anchor, the amber ST64 bulb at bulb_centre (no shadow, not in ray
	# tracing), a shadowed point light in it (venue-dive-bar 4.2: 300 lm, 2200 K, Source Radius 1.5 cm, Length 3 cm)
	p_meta = metas.get("Pendant_BarCone", {})
	p_hint = p_meta.get("placement_hint_ue_cm", {}).get("locations", [[x, 195.0, 274.0] for x in (300.0, 500.0, 700.0, 900.0)])
	p_bulb = p_meta.get("anchors_ue_m", {}).get("bulb_centre", [0.0, 0.0, -0.79])
	for k, loc in enumerate(p_hint):
		yaw = rng.uniform(0, 360)
		prop("Pendant_BarCone", tuple(loc), yaw, label=f"Pendant_L{5 + k}", metas=metas)
		b = local_to_world(tuple(loc), yaw, [c * 100.0 for c in p_bulb])
		prop("LampBulb_ST64", b, yaw, label=f"PendantBulb_L{5 + k}", shadow=False, ray_tracing=False, metas=metas)
		light(unreal.PointLight, f"LT_DB_L{5 + k}", b, lumens=300.0, kelvin=2200.0, radius=1.5, length=3.0, attenuation=900.0)
	# L13-L15 booth sconces: dusty frosted glass on the right wall at Z 1.50, a point light inside (250 lm, 2400 K, 3 cm)
	s_meta = metas.get("Sconce_Frosted", {})
	s_hint = s_meta.get("placement_hint_ue_cm", {})
	s_light = s_meta.get("anchors_ue_m", {}).get("light_centre", [0.13, 0.0, 1.48])
	for k, loc in enumerate(s_hint.get("locations", [[x, 732.0, 0.0] for x in (309.5, 502.5, 695.5)])):
		yaw = s_hint.get("yaw_deg", -90.0)
		prop("Sconce_Frosted", tuple(loc), yaw, label=f"Sconce_L{13 + k}", shadow=False, ray_tracing=False, metas=metas)
		light(unreal.PointLight, f"LT_DB_L{13 + k}", local_to_world(tuple(loc), yaw, [c * 100.0 for c in s_light]), lumens=250.0,
			kelvin=2400.0, radius=3.0, attenuation=800.0)
	# clutter: drinks on the bar and ledges, the coin dish, chalk, quarters on the rail (A6), a pint at Deacon's spot
	bar_items = [("Glass_Pint_Beer", 855.0, 205.0), ("Bottle_OldCastor", 820.0, 190.0), ("Coin_Quarter", 848.0, 214.0),
		("Glass_Rocks", 700.0, 200.0), ("Bottle_Hollenbeck", 520.0, 198.0), ("Glass_Pint", 540.0, 180.0), ("Can_LanternFlats", 380.0, 205.0),
		("Glass_Shot", 395.0, 185.0), ("Ashtray_Glass", 640.0, 214.0)]
	for k, (asset, x, y) in enumerate(bar_items):
		prop(asset, (x, y, 107.0), rng.uniform(0, 360), label=f"BarItem_{k}_{asset}", profile="RbVenueProp", metas=metas)
	for k in range(6):
		prop("Coin_Quarter", (640.0 + rng.uniform(-2.5, 2.5), 214.0 + rng.uniform(-2.5, 2.5), 107.0 + 1.3 + 0.18 * k), rng.uniform(0, 360),
			label=f"CoinDish_{k}", profile="RbVenueProp", metas=metas, pitch=rng.uniform(-6, 6))
	shelf_items = [("Glass_Pint_Beer", 1372.0 - 4.0, 366.0 - 14.0), ("Ashtray_Glass", 1372.0 - 12.0, 366.0 + 10.0), ("ChalkCube", 1372.0 + 13.0, 366.0 - 5.0)]
	for k, (asset, x, y) in enumerate(shelf_items):
		prop(asset, (x, y, 112.0), rng.uniform(0, 360), label=f"ShelfItem_{k}", profile="RbVenueProp", metas=metas)
	for k in range(4):
		prop("Coin_Quarter", (1372.0 - 12.0 + rng.uniform(-2, 2), 366.0 + 10.0 + rng.uniform(-2, 2), 112.0 + 1.4 + 0.18 * k), rng.uniform(0, 360),
			label=f"ShelfCoin_{k}", profile="RbVenueProp", metas=metas, pitch=rng.uniform(-5, 5))
	for k, (asset, x, y) in enumerate((("Bottle_OldCastor", 1180.0, 14.0), ("Glass_Pint", 1450.0, 12.0), ("Bottle_Hollenbeck", 1640.0 - 8.0, 610.0),
			("Can_OldCastor", 1640.0 - 9.0, 668.0))):
		prop(asset, (x, y, 107.0), rng.uniform(0, 360), label=f"LedgeItem_{k}", profile="RbVenueProp", metas=metas)
	# chalk cubes on the long rails (rail top 0.791), the quarters queue on the wall-side rail near the foot-right corner (A6)
	# on the flat cap, clear of the cushion (rail Y 4.754 - 4.919 incl. ~5 cm of cushion: Y 4.91 hung over the cushion's slope)
	prop("ChalkCube", (1340.0, 482.0, 79.1), 12.0, label="Chalk_RailL", profile="RbVenueProp", metas=metas)
	prop("ChalkCube", (1420.0, 601.8, 79.1), -20.0, label="Chalk_RailR", profile="RbVenueProp", metas=metas)
	for s in range(3):
		for k in range(4 + s):
			prop("Coin_Quarter", (1438.0 + 4.0 * s + rng.uniform(-0.1, 0.1), 601.8 + rng.uniform(-0.1, 0.1), 79.1 + 0.175 * k), rng.uniform(0, 360),
				label=f"Queue_{s}_{k}", profile="RbVenueProp", metas=metas)
	for k, (asset, x, y) in enumerate((("Glass_Pint_Beer", 300.0, 660.0), ("Can_Hollenbeck", 318.0, 700.0), ("Ashtray_Glass", 505.0, 700.0),
			("Glass_Rocks", 690.0, 660.0))):
		prop(asset, (x, y, 76.0), rng.uniform(0, 360), label=f"BoothItem_{k}", profile="RbVenueProp", metas=metas)


DECAL_DIR = "/Game/Generated/Venues/DiveBar/Decals"


def decal(label: str, set_name: str, cell: int, loc, facing: str, size_m: float, spin: float = 0.0, depth_cm: float = 4.0,
		variant: str = "", aspect: float = 1.0):
	"""One projected DBuffer decal of the first decal set (venue-dive-bar 7: floor, walls, ceiling only). The atlas cell comes from the
	decal colour (M_DB_Decal: cell = floor(R x 16)); facing is the surface the decal lies on: floor, ceiling, or the wall normal
	+X / -X / +Y / -Y (the direction the wall faces into the room); spin rotates it in the surface plane."""
	pitch, yaw, roll = {"floor": (-90.0, spin, 0.0), "ceiling": (90.0, spin, 0.0), "+Y": (0.0, -90.0, spin), "-Y": (0.0, 90.0, spin),
		"+X": (0.0, 180.0, spin), "-X": (0.0, 0.0, spin)}[facing]
	material = f"{DECAL_DIR}/MI_DB_Decal_{set_name}{'_' + variant if variant else ''}_01"
	half = size_m * 50.0
	DECAL_PLACEMENTS.append({"label": label, "set": set_name, "variant": variant, "material": material, "cell": cell,
		"decal_color_r": round((cell + 0.5) / 16.0, 5), "location_cm": [round(c, 2) for c in loc],
		"rotation_pyr_deg": [round(pitch, 3), round(yaw, 3), round(roll, 3)], "decal_size_cm": [depth_cm, round(half, 2), round(half * aspect, 2)],
		"surface": facing})
	actor = rb.spawn(unreal.DecalActor, loc, (pitch, yaw, roll), label)
	comp = actor.get_component_by_class(unreal.DecalComponent)
	mi = unreal.load_asset(material)
	if mi is None:
		rb.fail(f"decal instance {material} missing (run rb_make_divebar_materials.py)")
	comp.set_decal_material(mi)
	comp.set_editor_property("decal_size", unreal.Vector(depth_cm, half, half * aspect))
	comp.set_editor_property("decal_color", unreal.LinearColor((cell + 0.5) / 16.0, 0.0, 0.0, 1.0))
	comp.set_editor_property("fade_screen_size", 0.002)
	actor.tags = ["RbDB_Decal", f"RbDB_Decal_{set_name}"]
	return actor


DECAL_PLACEMENTS: list = []
DECAL_JSON = os.path.join(PROJECT, "Art", "DiveBar", "Export", "Decals", "decal_placements.json")


def write_decal_placements() -> None:
	"""The hand-off to M2-A's level generator: every projected decal of the first set as data (material, atlas cell as decal colour R,
	venue-frame location in cm, rotation, DecalSize half extents in cm). Deterministic (seeded)."""
	data = {"owner": "M2-B", "frame": "venue V (UE world, cm)", "note": "spawn an ADecalActor per entry: DecalMaterial = material, "
		"DecalSize = decal_size_cm, DecalColor = (decal_color_r, 0, 0, 1), tags RbDB_Decal + RbDB_Decal_<set>; generated by "
		"Tools/unreal/editor/rb_dev_m2b.py", "decals": DECAL_PLACEMENTS}
	with open(DECAL_JSON, "w", encoding="utf-8", newline="\n") as handle:
		handle.write(json.dumps(data, indent=1, sort_keys=True) + "\n")
	rb.log(f"wrote {DECAL_JSON} ({len(DECAL_PLACEMENTS)} decals)")


def decals(rng: random.Random) -> int:
	"""The first decal set in the room (venue-dive-bar 7; 9.1 R1 / R5 / S10 / S17): chalk dust around the table (densest at the
	corners), heel marks along the bar front and the table stance zone, dried spills near the bar front and booth B2 (+ the sticky
	roughness-only variant), kick scuffs on the bar die, the S10 cue-butt dings band at 0.80 - 1.00 m at the tight spots, blue chalk
	fingerprints around the cue rack, ceiling leak stains above the corridor opening and booth B1, a drip trail on the paneling.
	Rings / burns on furniture tops are bombed in the coated master (M_DB_Coated RingDensity / BurnDensity), not projected."""
	n = 0
	# chalk dust on the floor: table cabinet X 1257.8 - 1494.0, Y 475.4 - 610.0 (corners densest, the side pockets lighter)
	spots = [(1258, 475, -1, -1), (1258, 610, -1, 1), (1494, 475, 1, -1), (1494, 610, 1, 1)]
	for k, (x, y, sx, sy) in enumerate(spots):
		for j in range(5):
			dx, dy = sx * rng.uniform(5, 40), sy * rng.uniform(5, 35)
			decal(f"DC_ChalkFloor_{k}_{j}", "Chalk", rng.randrange(0, 8), (x + dx, y + dy, 0.0), "floor", rng.uniform(0.12, 0.22),
				rng.uniform(0, 360), variant="Floor")
			n += 1
	for k, (x, y) in enumerate(((1376, 462), (1376, 624), (1240, 543))):
		decal(f"DC_ChalkFloorSide_{k}", "Chalk", rng.randrange(0, 8), (x + rng.uniform(-10, 10), y, 0.0), "floor", rng.uniform(0.12, 0.2),
			rng.uniform(0, 360), variant="Floor")
		n += 1
	# heel marks: bar front (stool zone) and the stance zone around the table
	for k in range(9):
		decal(f"DC_Heel_Bar_{k}", "Scuffs", rng.randrange(0, 8), (rng.uniform(240, 920), rng.uniform(236, 330), 0.0), "floor",
			rng.uniform(0.18, 0.3), rng.uniform(0, 360))
		n += 1
	for k in range(8):
		a = rng.uniform(0, 2.0 * math.pi)
		decal(f"DC_Heel_Table_{k}", "Scuffs", rng.randrange(0, 8), (1376 + math.cos(a) * rng.uniform(150, 190),
			543 + math.sin(a) * rng.uniform(95, 120), 0.0), "floor", rng.uniform(0.18, 0.28), rng.uniform(0, 360))
		n += 1
	# dried spills (tide lines) near the bar front and booth B2, two sticky roughness-only patches in the traffic lane
	for k, (x, y, s) in enumerate(((520, 262, 0.55), (760, 250, 0.45), (870, 300, 0.4), (505, 590, 0.5))):
		decal(f"DC_Spill_{k}", "Stains", rng.randrange(0, 12), (x, y, 0.0), "floor", s, rng.uniform(0, 360))
		n += 1
	for k, (x, y) in enumerate(((650, 300), (980, 420))):
		decal(f"DC_Sticky_{k}", "Stains", rng.randrange(0, 12), (x, y, 0.0), "floor", 0.7, rng.uniform(0, 360), variant="Sticky")
		n += 1
	# kick scuffs on the bar die (front face Y 2.13 facing +Y, 0.05 - 0.30 m)
	for k in range(6):
		decal(f"DC_KickDie_{k}", "Scuffs", rng.randrange(8, 12), (rng.uniform(250, 900), 213.0, rng.uniform(12, 22)), "+Y",
			rng.uniform(0.22, 0.3), rng.uniform(-20, 20), depth_cm=5.0)
		n += 1
	# S10: cue-butt dings at 0.80 - 1.00 m where the room is tight (right wall by the rail, back wall behind the foot end)
	for k, x in enumerate((1275.0, 1318.0, 1452.0, 1488.0)):
		decal(f"DC_Dings_R_{k}", "Scuffs", rng.randrange(12, 16), (x, 732.0, rng.uniform(84, 96)), "-Y", rng.uniform(0.22, 0.3),
			rng.uniform(-8, 8), depth_cm=3.0, variant="Dings")
		n += 1
	for k, y in enumerate((505.0, 548.0, 590.0)):
		decal(f"DC_Dings_B_{k}", "Scuffs", rng.randrange(12, 16), (1646.0, y, rng.uniform(84, 96)), "-X", rng.uniform(0.22, 0.3),
			rng.uniform(-8, 8), depth_cm=3.0, variant="Dings")
		n += 1
	# blue chalk fingerprints around the cue rack (rack X 13.30 - 14.30 on the right wall)
	for k, (x, z) in enumerate(((1324, 128), (1327, 96), (1436, 140), (1433, 110), (1380, 166), (1300, 110))):
		decal(f"DC_ChalkPrint_{k}", "Chalk", rng.randrange(8, 16), (x, 732.0, z), "-Y", rng.uniform(0.07, 0.1), rng.uniform(0, 360),
			depth_cm=3.0)
		n += 1
	# ceiling leak stains: above the corridor opening and above booth B1; a drip trail down the paneling by the dart machine
	for k, (x, y, s) in enumerate(((1590, 90, 0.9), (1540, 150, 0.6), (300, 640, 0.8))):
		decal(f"DC_Leak_{k}", "Water", rng.randrange(0, 16), (x, y, 274.0), "ceiling", s, rng.uniform(0, 360), depth_cm=6.0)
		n += 1
	decal("DC_Drip_Paneling", "Stains", rng.randrange(12, 16), (1005.0, 732.0, 95.0), "-Y", 0.45, 0.0, depth_cm=3.0, aspect=1.6)
	n += 1
	rb.log(f"decals: {n} projected (budget <= 120 in view)")
	return n


def table() -> None:
	t = rb.spawn(unreal.RbTable, TABLE_LOC, (0.0, 0.0, 0.0), "Table")
	t.set_editor_property("preset", unreal.RbTablePreset.SEVEN_FOOT_BAR)
	t.set_editor_property("ball_set", unreal.RbBallSetPreset.OLD_BAR_OVERSIZED_CUE)
	t.set_editor_property("use_baked_meshes", True)
	t.set_editor_property("lamp_underside_height", 0.86)
	t.tags = ["RbPlayerTable"]


def lights() -> None:
	"""The Open state of venue-dive-bar 4.2 (subset; physical units). M2-A's lights.json is the real rig."""
	# (L5-L8 bar pendants and L13-L15 sconces sit in their fixtures: props())
	# L9 / L10 back-bar strips: under every glass shelf (the level has two strips; the dev rig spreads the flux per shelf)
	for bay in range(4):
		yb = -190.0 * 1.5 + 190.0 * bay
		for zs in (122.5 - 1.8, 155.5 - 1.8, 188.5 - 1.8):
			light(unreal.RectLight, f"LT_DB_L9_{bay}_{int(zs)}", (564.0 + yb, 21.0, zs), (-90.0, 0.0, 0.0), lumens=250.0, kelvin=2700.0,
				width=180.0, height=2.0, shadows=False, attenuation=300.0)
	# L11 / L12 cooler glow: just in front of the fogged doors, shadowed (the bar counter keeps it on the bartender's side)
	light(unreal.RectLight, "LT_DB_L11", (564.0 + (-2.515) * 100.0 + 0.0, 68.0, 45.0), (0.0, 90.0, 0.0), lumens=1200.0, kelvin=5000.0,
		width=160.0, height=60.0, shadows=True, attenuation=400.0)
	light(unreal.RectLight, "LT_DB_L12", (564.0 + 226.0, 68.0, 45.0), (0.0, 90.0, 0.0), lumens=830.0, kelvin=5000.0, width=110.0, height=60.0,
		shadows=True, attenuation=400.0)
	light(unreal.RectLight, "LT_DB_L18", (1130.0, 732.0 - 72.0, 90.0), (0.0, -90.0, 0.0), lumens=200.0, color=(1.0, 0.55, 0.25), width=60.0,
		height=90.0, shadows=False, specular=0.0, attenuation=500.0)
	light(unreal.RectLight, "LT_DB_L19", (890.0, 732.0 - 44.0, 160.0), (0.0, -90.0, 0.0), lumens=120.0, color=(1.0, 0.35, 0.3), width=50.0,
		height=30.0, shadows=False, specular=0.0, attenuation=500.0)
	# neon proxies (4.3: flux = tube flux / pi, Specular 0): N3 Hollenbeck, N4 POOL, N5 Lantern Flats, N1 / N2 window signs
	light(unreal.RectLight, "LT_DB_N3", (850.0, 6.0, 241.0), (0.0, 90.0, 0.0), lumens=535.0, color=(0.55, 0.7, 1.0), width=170.0, height=42.0,
		specular=0.0, scattering=0.5, attenuation=800.0)
	light(unreal.RectLight, "LT_DB_N4", (1380.0, 728.0, 217.0), (0.0, -90.0, 0.0), lumens=410.0, color=(1.0, 0.42, 0.18), width=80.0,
		height=45.0, specular=0.0, scattering=0.5, attenuation=800.0)
	light(unreal.RectLight, "LT_DB_N5", (1642.0, 290.0, 167.0), (0.0, 180.0, 0.0), lumens=145.0, color=(0.25, 0.35, 1.0), width=100.0,
		height=45.0, specular=0.0, scattering=0.5, attenuation=700.0)
	light(unreal.RectLight, "LT_DB_N1", (8.0, 230.0, 132.0), (0.0, 0.0, 0.0), lumens=580.0, color=(1.0, 0.3, 0.12), width=100.0, height=50.0,
		specular=0.0, scattering=0.5, attenuation=900.0)
	light(unreal.RectLight, "LT_DB_N2", (8.0, 350.0, 132.0), (0.0, 0.0, 0.0), lumens=340.0, color=(0.7, 0.35, 0.9), width=80.0, height=40.0,
		specular=0.0, scattering=0.5, attenuation=900.0)
	# TV-1 over the bar (blue-cold content glow; the TV itself is later)
	light(unreal.RectLight, "LT_DB_L16", (680.0, 40.0, 230.0), (0.0, 90.0, 0.0), lumens=90.0, kelvin=8000.0, width=121.0, height=68.0,
		shadows=False, attenuation=700.0)
	sky = rb.spawn(unreal.SkyLight, (800, 360, 250), label="SkyLight")
	sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
	sky.light_component.set_editor_property("intensity", 0.0)
	fog = rb.spawn(unreal.ExponentialHeightFog, (800.0, 360.0, 0.0), label="HeightFog")
	fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
	fc.set_editor_property("fog_density", 0.003)
	fc.set_editor_property("fog_height_falloff", 0.001)
	fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
	fc.set_editor_property("enable_volumetric_fog", True)
	fc.set_editor_property("volumetric_fog_scattering_distribution", 0.6)
	fc.set_editor_property("volumetric_fog_albedo", unreal.Color(242, 235, 224, 255))
	fc.set_editor_property("volumetric_fog_extinction_scale", 1.0)


CAMERAS = [
	# (tag, eye (venue m), target (venue m), what)
	("M2B_V01", (0.90, 6.00, 1.65), (12.00, 4.50, 1.00), "DB-3 V01 entrance: full depth, bar, booths, the table at the back"),
	("M2B_V02", (8.60, 2.62, 1.55), (13.76, 5.43, 0.85), "DB-3 V02 Deacon's stool: the table as a regular sees it"),
	("M2B_V03", (12.00, 5.43, 1.62), (14.30, 5.43, 0.76), "DB-3 V03 head end, standing"),
	("M2B_V05", (5.20, 2.90, 1.60), (5.20, 0.30, 1.50), "DB-3 V05 back bar"),
	("M2B_V06", (14.50, 5.18, 0.80), (14.27, 5.43, 0.77), "DB-3 V06 ball macro (racked balls, the lamp in the reflections)"),
	("M2B_V07", (15.40, 5.60, 1.10), (14.94, 5.43, 0.60), "DB-3 V07 coin slide (the cabinet is M2-L's)"),
	("M2B_V09", (13.90, 6.95, 1.50), (13.90, 5.43, 0.76), "DB-3 V09 wall side (cue rack at the back of the aisle)"),
	("M2B_BarRun", (9.30, 3.30, 1.60), (3.0, 1.9, 0.95), "bar counter, stools, back bar from the flap end"),
	("M2B_Stools", (7.75, 3.55, 1.05), (8.55, 2.55, 0.62), "hero: bar stools #8 - #10 (torn, taped, newer)"),
	("M2B_BarTop", (8.25, 2.95, 1.42), (8.62, 2.05, 1.07), "hero: bar top at Deacon's spot (plate, pint, rings)"),
	("M2B_BackBarClose", (6.30, 1.35, 1.55), (6.95, 0.10, 1.45), "hero: bottles on the sagging shelves, mirror, LED strips"),
	("M2B_Coolers", (3.30, 1.45, 0.75), (3.10, 0.40, 0.45), "hero: under-counter cooler, fogged doors"),
	("M2B_Lamp", (12.55, 4.35, 1.45), (13.76, 5.45, 1.72), "hero: the 3-shade lamp (logo, dent, chains, cord)"),
	("M2B_Pendants", (6.40, 3.05, 1.50), (4.60, 1.95, 1.85), "mid: bar mini pendants L5-L8 (black cones, amber ST64 bulbs)"),
	("M2B_Sconces", (6.30, 5.65, 1.55), (5.03, 7.20, 1.45), "mid: booth sconces L13-L15 (frosted glass) over booth B3 / B2"),
	("M2B_CueRack", (13.20, 5.95, 1.35), (13.85, 7.30, 0.95), "hero: wall cue rack with house cues and the bridge"),
	("M2B_Jukebox", (10.75, 5.35, 1.45), (11.30, 7.10, 0.90), "hero: jukebox"),
	("M2B_Dart", (8.55, 4.75, 1.62), (8.90, 7.10, 1.40), "mid: dart machine"),
	("M2B_Booths", (8.55, 5.55, 1.62), (4.60, 6.85, 0.75), "mid: booths"),
	("M2B_ColumnShelf", (12.75, 2.70, 1.45), (13.72, 3.70, 1.08), "hero: column shelf with the coin dish, pint, chalk"),
	("M2B_Ledge", (12.40, 1.35, 1.40), (13.60, 0.12, 1.02), "mid: left-wall ledge and spectator stools"),
	("M2B_DB0_Stool", (9.25, 3.45, 1.05), (8.60, 2.62, 0.48), "DB-0: stool #10 end to end (Blender -> FBX -> Interchange -> MI_DB_*)"),
	("M2B_Flyers", (9.95, 5.70, 1.55), (10.10, 7.32, 1.45), "signs: league and band flyers between the dart machine and the jukebox"),
	("M2B_LowClearance", (14.30, 1.70, 1.60), (16.46, 0.85, 2.25), "signs: S1 LOW CLEARANCE 10'-6\" over the corridor, RESTROOMS"),
	("M2B_FloorChalk", (15.95, 6.95, 1.55), (14.90, 5.95, 0.10), "decals: chalk dust at the foot-right corner, S10 dings, rack prints"),
	("M2B_BarFront", (6.30, 4.40, 1.20), (5.30, 2.25, 0.20), "decals: kick scuffs on the bar die, heel marks, dried spills"),
]


def cameras() -> None:
	for tag, eye, target, _ in CAMERAS:
		e = (eye[0] * 100.0, eye[1] * 100.0, eye[2] * 100.0)
		t = (target[0] * 100.0, target[1] * 100.0, target[2] * 100.0)
		cam = rb.spawn(unreal.RbLookDevCamera, e, rb.look_at_rotation(e, t), tag)
		cam.tags = [tag]
		cam.set_editor_property("preset", unreal.RbCameraPreset.EYES)
		cam.set_editor_property("focus_distance_cm", 0.0)
	# V04 chin on cue (the rig's own down-on-shot eye from the cue axis; the cue ball behind the head string, aimed at the apex)
	cam = rb.spawn(unreal.RbLookDevCamera, (1278.0, 543.0, 90.0), (0.0, 0.0, 0.0), "M2B_V04")
	cam.tags = ["M2B_V04"]
	cam.set_editor_property("preset", unreal.RbCameraPreset.EYES)
	cam.set_editor_property("placement", unreal.RbLookDevPlacement.CHIN_ON_CUE)
	cam.set_editor_property("cue_ball_core", unreal.Vector2D(-0.70, 0.10))
	cam.set_editor_property("aim_point_core", unreal.Vector2D(0.508, 0.0))


def build_level(metas: dict) -> None:
	rb.ensure_dir(DEV)
	if unreal.EditorAssetLibrary.does_asset_exist(MAP):
		world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
		if world is None or not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
			rb.fail(f"could not replace {MAP}")
	else:
		rb.new_level(MAP)
	world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
	world.get_world_settings().set_editor_property("default_game_mode", unreal.RbGameMode)
	dm = {
		"plaster_cream": dev_mi("MI_DB_Plaster_Cream", "World", {"WorldUV": 1.0}),
		"plaster_ox": dev_mi("MI_DB_Plaster_Oxblood", "World", {"WorldUV": 1.0}),
		"paneling": dev_mi("MI_DB_Paneling_Dark", "World", {"WorldUV": 1.0}),
		"brick": dev_mi("MI_DB_Brick_PaintedGreen", "World", {"WorldUV": 1.0}),
		"block": dev_mi("MI_DB_Block_Painted", "World", {"WorldUV": 1.0}),
		"ceiling": dev_mi("MI_DB_CeilingTile", "World", {"WorldUV": 1.0}),
		"blacksteel": dev_mi("MI_DB_Paint_BlackSteel", "World", {"WorldUV": 1.0, "NormalStrength": 0.25, "TileSizeM": 0.6}),
		"door": dev_mi("MI_DB_Paint_Cabinet", "World", {"WorldUV": 1.0}),
	}
	rng = random.Random(1958)
	shell(dm)
	props(metas, rng)
	DECAL_PLACEMENTS.clear()
	decals(random.Random(2006))
	write_decal_placements()
	table()
	lights()
	cameras()
	rb.spawn(unreal.PlayerStart, (1220.0, 543.0, 100.0), (0.0, 0.0, 0.0), "PlayerStart")
	rb.save_current_level(MAP)


def load_metas() -> dict:
	metas = {}
	for path in asset_jsons():
		with open(path, "r", encoding="utf-8") as handle:
			meta = json.load(handle)
		metas[meta["asset_id"]] = meta
	return metas


def main() -> None:
	argv = sys.argv[1:]
	if "--list" in argv:
		for tag, _, _, what in CAMERAS + [("M2B_V04", None, None, "DB-3 V04 chin on cue")]:
			print(f"python Tools/unreal/rbue.py capture --map \"{MAP}{MATCH_OPTIONS}\" --camera {tag} --warmup-seconds 12 "
				f"--exec-cmds \"rb.Quality High\" --out Docs/images/dev/m2b/{tag[4:]}.png   # {what}")
		return
	if "--ledger-negative" in argv:
		ledger_negative()
		return
	only = []
	if "--only" in argv:
		for arg in argv[argv.index("--only") + 1:]:
			if arg.startswith("--"):
				break
			only.append(arg)
	metas = load_metas()
	if "--level-only" not in argv:
		metas.update(import_all(only))
	ledger_negative()
	if "--import-only" not in argv:
		build_level(metas)
	rb.log("rb_dev_m2b: OK")


main()
