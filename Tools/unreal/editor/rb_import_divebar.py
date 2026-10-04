"""Imports the Blender exports of a venue into Unreal (M2-A pipeline, used by M2-A / M2-B / M2-L; venue-dive-bar 13.8).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_import_divebar.py [-- --only <AssetId> ... --export-dir <dir> --check-only]

Scans Art/DiveBar/Export/**/<Asset>/<Asset>.json (one metadata file per asset, written by the Blender generators through
Tools/blender/common/rb_bl.py; NO shared manifest, so generators of different packages never edit the same file) and for each:
  * refuses the asset without a licence-ledger row (Docs/licenses/asset-ledger.csv or a package fragment
    Docs/licenses/ledger/<package>.csv) for the asset itself (SM_DB_<Asset>) and for every external input it lists, or with an
    NC licence - the DB-0 negative test (RawBreak ledger check: a refused asset fails the run);
  * imports the FBX through Interchange (automated, replace existing, materials and textures off, UCX collision by name, one
    convex hull per UCX) to /Game/Generated/Venues/DiveBar/<ue_folder>/SM_DB_<Asset> (ue_folder from the metadata: M2-A's
    Arch / Lighting, M2-B's Props);
  * sets Nanite (full-detail fallback, ARCH 2.2) on opaque meshes, 3 auto LODs on translucent ones (50 / 25 / 12 %), the
    collision profile named in the metadata (RbVenueBlock / RbVenueProp) as the mesh's default, the physical material, and the
    material of each slot by its name: M2-B's MI_DB_<...> (/Game/Generated/Venues/DiveBar/Materials), a table-family MI_Rb* of
    M2-L (/Game/Generated/Materials), else an M2-A fallback instance of M_DBA_Fallback (procedural surface patterns in
    /Game/Generated/Venues/DiveBar/Arch/Fallback; only while the real material does not exist);
  * re-checks the bounds against the metadata (VDB-T4: the import must reproduce the exporter's bounds; target dimensions
    within +-2 mm hero / +-1 cm others).
Idempotent. --check-only runs the ledger / metadata checks without importing (also usable on the host: see ledger_check()).
Owner: M2-A.
"""

from __future__ import annotations

import csv
import glob
import json
import math
import os
import sys

try:
	import unreal
except ImportError:  # host-side use of ledger_check()
	unreal = None

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXPORT_DIR = os.path.join(REPO, "Art", "DiveBar", "Export")
VENUE_ROOT = "/Game/Generated/Venues/DiveBar"
MATERIALS_B = "/Game/Generated/Venues/DiveBar/Materials"   # M2-B: MI_DB_*
MATERIALS_L = "/Game/Generated/Materials"                  # M2-L: MI_Rb*
FALLBACK_DIR = "/Game/Generated/Venues/DiveBar/Arch/Fallback"
FALLBACK_MASTER = f"{FALLBACK_DIR}/M_DBA_Fallback"
PHYS_DIR = "/Game/Generated/Physics"
NC_MARKERS = ("-NC", "NC-", "NonCommercial", "Non-Commercial")


# ------------------------------------------------------------------------------------------------------------------------------
# Ledger + metadata checks (plain Python, host-usable)
# ------------------------------------------------------------------------------------------------------------------------------


def load_ledger(repo: str = REPO) -> dict:
	"""asset_id -> row over Docs/licenses/asset-ledger.csv and every fragment Docs/licenses/ledger/*.csv."""
	rows = {}
	files = [os.path.join(repo, "Docs", "licenses", "asset-ledger.csv")] + sorted(glob.glob(os.path.join(repo, "Docs", "licenses", "ledger", "*.csv")))
	for path in files:
		if not os.path.exists(path):
			continue
		with open(path, "r", encoding="utf-8", newline="") as handle:
			for row in csv.DictReader(handle):
				if row.get("asset_id"):
					rows[row["asset_id"].strip()] = row
	return rows


def find_assets(export_dir: str = EXPORT_DIR) -> list:
	"""Every <dir>/<Asset>/<Asset>.json below export_dir (nested folders allowed), sorted."""
	found = []
	for root, _, files in os.walk(export_dir):
		name = os.path.basename(root)
		if f"{name}.json" in files:
			found.append(os.path.join(root, f"{name}.json"))
	return sorted(found)


def ledger_check(meta: dict, ledger: dict) -> list:
	"""Reasons to refuse the asset (empty = OK): its own row, one row per external input, no NC licence."""
	problems = []
	asset_key = meta.get("mesh") or f"SM_DB_{meta.get('asset_id', '?')}"
	keys = [asset_key] + [str(i) for i in meta.get("external_inputs", [])]
	for key in keys:
		row = ledger.get(key)
		if row is None:
			problems.append(f"no licence-ledger row for {key}")
			continue
		licence = row.get("licence", "")
		if any(m.lower() in licence.lower() for m in NC_MARKERS):
			problems.append(f"{key}: non-commercial licence '{licence}'")
	return problems


def metadata_check(meta: dict, json_path: str) -> list:
	problems = []
	for key in ("asset_id", "fbx", "bounds_min_m", "bounds_max_m"):
		if key not in meta:
			problems.append(f"metadata lacks '{key}'")
	fbx = os.path.join(os.path.dirname(json_path), meta.get("fbx", ""))
	if not os.path.isfile(fbx):
		problems.append(f"missing FBX {fbx}")
	if "target_m" in meta:
		size = [meta["bounds_max_m"][i] - meta["bounds_min_m"][i] for i in range(3)]
		tol = float(meta.get("tolerance_m", 0.01))
		for axis in range(3):
			if abs(size[axis] - meta["target_m"][axis]) > tol + 1e-6:
				problems.append(f"VDB-T4 {'XYZ'[axis]} {size[axis]:.4f} m vs target {meta['target_m'][axis]:.4f} m (+-{tol})")
	return problems


# ------------------------------------------------------------------------------------------------------------------------------
# Unreal side
# ------------------------------------------------------------------------------------------------------------------------------


def _log(msg: str) -> None:
	if unreal:
		unreal.log(f"[rb] {msg}")
	else:
		print(f"[rb] {msg}")


def _fail(msg: str) -> None:
	if unreal:
		unreal.log_error(f"RBUE_FAIL {msg}")
	raise RuntimeError(msg)


def _ensure_dir(path: str) -> None:
	if not unreal.EditorAssetLibrary.does_directory_exist(path):
		unreal.EditorAssetLibrary.make_directory(path)


def _set(obj, name: str, value) -> bool:
	try:
		obj.set_editor_property(name, value)
		return True
	except Exception:  # noqa: BLE001 - optional pipeline properties differ between engine versions
		return False


PIPELINE_DIR = "/Game/Dev/M2A/Pipelines"  # scratch (git-ignored); recreated per run
DEFAULT_PIPELINES = ("/Interchange/Pipelines/DefaultFBXOBJAssetsPipeline", "/Interchange/Pipelines/DefaultAssetsPipeline")


_PIPELINES: dict = {}


def make_pipeline_asset(build_nanite: bool, collision: bool = True) -> str:
	"""A configured copy of the engine's default Interchange assets pipeline (ImportAssetParameters take pipeline ASSETS by path).
	Made once per run. collision=False for meshes without UCX hulls (no generated box / convex: a lamp shade must not block the
	light's line of sight, clutter must not stop the pawn)."""
	name = ("RbDB_ImportPipeline_Nanite" if build_nanite else "RbDB_ImportPipeline") + ("" if collision else "_NoCol")
	path = f"{PIPELINE_DIR}/{name}"
	if name in _PIPELINES:
		return path
	_PIPELINES[name] = True
	_ensure_dir(PIPELINE_DIR)
	if unreal.EditorAssetLibrary.does_asset_exist(path):
		unreal.EditorAssetLibrary.delete_asset(path)
	asset = None
	for source in DEFAULT_PIPELINES:
		template = unreal.load_asset(source)
		if template is not None:
			asset = unreal.EditorAssetLibrary.duplicate_loaded_asset(template, path)
			if asset is not None:
				break
	if asset is None:
		_fail(f"could not duplicate an Interchange default pipeline {DEFAULT_PIPELINES}")
	configure_pipeline(asset, build_nanite, collision)
	unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
	return path


def make_pipeline(build_nanite: bool, collision: bool = True):
	pipeline = unreal.InterchangeGenericAssetsPipeline()
	configure_pipeline(pipeline, build_nanite, collision)
	return pipeline


def configure_pipeline(pipeline, build_nanite: bool, collision: bool = True) -> None:
	mesh = pipeline.get_editor_property("mesh_pipeline")
	for key, value in (("import_static_meshes", True), ("import_skeletal_meshes", False), ("combine_static_meshes", True),
			("import_collision", collision), ("import_collision_according_to_mesh_name", collision), ("one_convex_hull_per_ucx", True),
			("build_nanite", build_nanite), ("generate_lightmap_u_vs", False), ("import_morph_targets", False)):
		_set(mesh, key, value)
	materials = pipeline.get_editor_property("material_pipeline")
	_set(materials, "import_materials", False)
	textures = materials.get_editor_property("texture_pipeline")
	if textures is not None:
		_set(textures, "import_textures", False)
	common = pipeline.get_editor_property("common_meshes_properties")
	for key, value in (("recompute_normals", False), ("recompute_tangents", True), ("remove_degenerates", False), ("import_lods", False),
			("keep_sections_separate", False), ("bake_meshes", True)):
		_set(common, key, value)
	_set(pipeline, "use_source_name_for_asset", False)
	return pipeline


def import_fbx(fbx: str, folder: str, name: str, build_nanite: bool, collision: bool = True):
	"""Interchange import of one FBX into folder/name (replace existing; the configured pipeline asset: no materials, no textures,
	UCX collision). Anything else the import leaves in the folder (materials, textures) is deleted. Returns the StaticMesh."""
	_ensure_dir(folder)
	path = f"{folder}/{name}"
	before = set(unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False))
	params = unreal.ImportAssetParameters()
	params.set_editor_property("is_automated", True)
	params.set_editor_property("replace_existing", True)
	params.set_editor_property("destination_name", name)
	params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(make_pipeline_asset(build_nanite, collision))])
	manager = unreal.InterchangeManager.get_interchange_manager_scripted()
	source = unreal.InterchangeManager.create_source_data(fbx)
	manager.import_asset(folder, source, params)
	manager.wait_until_all_tasks_done(False)  # False: wait, do not cancel (True cancels queued imports)
	mesh = unreal.load_asset(path)
	if not isinstance(mesh, unreal.StaticMesh):
		_fail(f"import of {fbx} produced no StaticMesh at {path}")
	for extra in sorted(set(unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False)) - before):
		if extra.split(".")[0] != path:
			_log(f"removing import by-product {extra}")
			unreal.EditorAssetLibrary.delete_asset(extra)
	return mesh


def _mesh_tools():
	"""StaticMeshEditorSubsystem when its module is loaded (editor), else the older EditorStaticMeshLibrary (commandlet)."""
	sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) if hasattr(unreal, "StaticMeshEditorSubsystem") else None
	return sms if sms is not None else unreal.EditorStaticMeshLibrary

# --- fallback materials (procedural, M2-A; replaced by M2-B's MI_DB_* as soon as they exist) ------------------------------------

# Pattern ids of the fallback master's HLSL (FALLBACK_BODY below).
PATTERN = {"flat": 0, "vct": 1, "brick": 2, "ceiling": 3, "wood": 4, "glassblock": 5, "plaster": 6, "block": 7, "tin": 8, "planks": 9,
	"metal": 10, "street": 11, "paneling": 12, "lens": 13, "wired": 14, "asphalt": 15, "concrete": 16, "fabric": 17, "enamel": 18,
	"tv": 19, "poster": 20, "photo": 21, "polaroid": 22, "glow": 23, "mirror": 24}

# slot name -> (pattern, albedo (linear), roughness, f0, extra) - dive-bar colours of venue-dive-bar 6.2 / 6.3 / 3.1, ESTIMATE.
FALLBACK_SURFACES = {
	"MI_DB_VCT_Oxblood": ("vct", (0.095, 0.015, 0.012), 0.40, 0.04, {"AltColor": (0.011, 0.011, 0.011), "Scale": 0.305}),
	"MI_DB_VCT_Black": ("vct", (0.012, 0.012, 0.012), 0.40, 0.04, {"AltColor": (0.010, 0.010, 0.010), "Scale": 0.305}),
	"MI_DB_VCT_Beige": ("vct", (0.40, 0.34, 0.24), 0.45, 0.04, {"AltColor": (0.37, 0.32, 0.22), "Scale": 0.305, "Variant": 1.0}),
	"MI_DB_Brick_PaintedGreen": ("brick", (0.030, 0.062, 0.040), 0.78, 0.04, {"AltColor": (0.13, 0.045, 0.028)}),
	"MI_DB_Plaster_Cream": ("plaster", (0.34, 0.27, 0.16), 0.85, 0.04, {"Variant": 1.0}),
	"MI_DB_Plaster_Oxblood": ("plaster", (0.070, 0.017, 0.015), 0.78, 0.04, {}),
	"MI_DB_Paneling_Dark": ("paneling", (0.075, 0.037, 0.017), 0.40, 0.045, {"AltColor": (0.034, 0.016, 0.008)}),
	"MI_DB_Block_Painted": ("block", (0.29, 0.25, 0.18), 0.82, 0.04, {}),
	"MI_DB_Mortar": ("concrete", (0.30, 0.29, 0.26), 0.90, 0.04, {}),
	"MI_DB_CeilingTile": ("ceiling", (0.26, 0.22, 0.15), 0.93, 0.04, {}),
	"MI_DB_Tin_Painted": ("tin", (0.32, 0.30, 0.25), 0.55, 0.04, {"AltColor": (0.13, 0.065, 0.03)}),
	"MI_DB_Paint_BlackSteel": ("metal", (0.018, 0.018, 0.019), 0.42, 0.05, {"AltColor": (0.11, 0.05, 0.025)}),
	"MI_DB_Paint_White": ("flat", (0.24, 0.21, 0.14), 0.45, 0.04, {}),
	"MI_DB_Paint_Cabinet": ("metal", (0.016, 0.016, 0.018), 0.30, 0.05, {"AltColor": (0.08, 0.08, 0.08)}),
	"MI_DB_Steel_Black": ("flat", (0.020, 0.020, 0.020), 0.45, 0.05, {}),
	"MI_DB_Plastic_Black": ("flat", (0.014, 0.014, 0.014), 0.45, 0.04, {}),
	"MI_DB_Rubber_Black": ("flat", (0.016, 0.016, 0.016), 0.75, 0.04, {}),
	"MI_DB_Rubber_Mat": ("fabric", (0.014, 0.014, 0.014), 0.85, 0.04, {}),
	"MI_DB_Wood_PlankWall": ("planks", (0.100, 0.052, 0.024), 0.50, 0.04, {"AltColor": (0.05, 0.026, 0.012)}),
	"MI_DB_Wood_Stained": ("wood", (0.080, 0.040, 0.018), 0.42, 0.04, {"AltColor": (0.036, 0.017, 0.008)}),
	"MI_DB_Wood_Lacquered_Bar": ("wood", (0.150, 0.058, 0.024), 0.16, 0.05, {"AltColor": (0.07, 0.024, 0.010)}),
	"MI_DB_Plywood_Painted": ("flat", (0.040, 0.030, 0.024), 0.60, 0.04, {}),
	"MI_DB_Laminate_Walnut": ("wood", (0.10, 0.055, 0.028), 0.35, 0.04, {"AltColor": (0.05, 0.026, 0.012)}),
	"MI_DB_Alu_Trim": ("flat", (0.62, 0.62, 0.60), 0.35, 0.90, {}),
	"MI_DB_Steel_Stainless": ("flat", (0.56, 0.57, 0.58), 0.25, 0.92, {}),
	"MI_DB_Steel_Zinc": ("flat", (0.62, 0.62, 0.59), 0.42, 0.90, {}),
	"MI_DB_Chrome": ("flat", (0.55, 0.56, 0.55), 0.08, 0.95, {}),
	"MI_DB_Chrome_Pitted": ("flat", (0.55, 0.56, 0.55), 0.12, 0.95, {}),
	"MI_DB_Brass_Worn": ("flat", (0.91, 0.78, 0.42), 0.32, 0.95, {}),
	"MI_DB_Glass_Clear": ("flat", (0.010, 0.012, 0.013), 0.04, 0.04, {}),
	"MI_DB_Glass_Wired": ("wired", (0.020, 0.024, 0.024), 0.08, 0.04, {}),
	"MI_DB_Glass_Cooler": ("flat", (0.03, 0.035, 0.04), 0.10, 0.04, {"Emissive": 60.0, "EmissiveColor": (0.85, 0.93, 1.0)}),
	"MI_DB_GlassBlock": ("glassblock", (0.030, 0.036, 0.040), 0.06, 0.04, {"Emissive": 2.5, "EmissiveColor": (0.78, 0.86, 1.0)}),
	"MI_DB_Street_Backplate": ("street", (0.0, 0.0, 0.0), 0.9, 0.04, {"Emissive": 1.0, "EmissiveColor": (1.0, 1.0, 1.0)}),
	"MI_DB_Asphalt_Wet": ("asphalt", (0.02, 0.02, 0.021), 0.20, 0.04, {}),
	"MI_DB_Concrete_Sidewalk": ("concrete", (0.16, 0.155, 0.15), 0.80, 0.04, {}),
	"MI_DB_Troffer_Lens": ("lens", (0.60, 0.60, 0.58), 0.30, 0.04, {"Emissive": 0.0, "EmissiveColor": (0.97, 0.98, 1.0)}),
	"MI_DB_Emissive_Exit": ("flat", (0.30, 0.02, 0.02), 0.30, 0.04, {"Emissive": 40.0, "EmissiveColor": (1.0, 0.04, 0.02)}),
	"MI_DB_Emissive_Bulb2700": ("flat", (0.8, 0.8, 0.78), 0.5, 0.04, {"Emissive": 30000.0, "EmissiveColor": (1.0, 0.70, 0.42)}),
	"MI_DB_Emissive_Bulb3000": ("flat", (0.8, 0.8, 0.78), 0.5, 0.04, {"Emissive": 30000.0, "EmissiveColor": (1.0, 0.76, 0.52)}),
	"MI_DB_Emissive_Bulb2200": ("flat", (0.8, 0.8, 0.78), 0.5, 0.04, {"Emissive": 12000.0, "EmissiveColor": (1.0, 0.56, 0.24)}),
	"MI_DB_Enamel_Green": ("enamel", (0.018, 0.055, 0.030), 0.28, 0.05, {"AltColor": (0.30, 0.28, 0.24)}),
	"MI_DB_Enamel_Black": ("enamel", (0.012, 0.012, 0.012), 0.30, 0.05, {"AltColor": (0.20, 0.19, 0.17)}),
	"MI_DB_Glass_Frosted": ("glow", (0.55, 0.52, 0.45), 0.45, 0.04, {"Emissive": 450.0, "EmissiveColor": (1.0, 0.66, 0.38)}),
	"MI_DB_Enamel_WhiteInt": ("enamel", (0.72, 0.70, 0.64), 0.25, 0.05, {"AltColor": (0.40, 0.36, 0.28)}),
	"MI_DB_Vinyl_Oxblood": ("fabric", (0.085, 0.016, 0.014), 0.45, 0.04, {}),
	"MI_DB_Greybox": ("flat", (0.30, 0.30, 0.30), 0.7, 0.04, {}),
	# M2-B's prop slots (db_*.py of M2-B) until rb_make_divebar_materials.py makes the real instances: opaque stand-ins (glass as tinted
	# opaque, liquids hidden inside), emissive panels at their 4.2 luminances.
	"MI_DB_Aluminium": ("flat", (0.60, 0.60, 0.58), 0.30, 0.90, {}),
	"MI_DB_Beer_Foam": ("fabric", (0.55, 0.50, 0.40), 0.60, 0.04, {}),
	"MI_DB_Ceramic_Glazed": ("flat", (0.55, 0.52, 0.45), 0.12, 0.05, {}),
	"MI_DB_Chalk_Blue": ("fabric", (0.05, 0.16, 0.40), 0.90, 0.04, {}),
	"MI_DB_CueShaft_Maple": ("wood", (0.45, 0.32, 0.18), 0.35, 0.04, {"AltColor": (0.35, 0.24, 0.13)}),
	"MI_DB_CuproNickel": ("flat", (0.55, 0.55, 0.52), 0.35, 0.90, {}),
	"MI_DB_Emissive_CoolerPanel": ("flat", (0.6, 0.6, 0.6), 0.5, 0.04, {"Emissive": 400.0, "EmissiveColor": (0.85, 0.93, 1.0)}),
	"MI_DB_Emissive_DartMarquee": ("flat", (0.3, 0.05, 0.04), 0.3, 0.04, {"Emissive": 250.0, "EmissiveColor": (1.0, 0.30, 0.25)}),
	"MI_DB_Emissive_JukeboxMarquee": ("flat", (0.4, 0.3, 0.2), 0.3, 0.04, {"Emissive": 120.0, "EmissiveColor": (1.0, 0.55, 0.20)}),
	"MI_DB_Emissive_JukeboxPanels": ("flat", (0.35, 0.12, 0.05), 0.3, 0.04, {"Emissive": 120.0, "EmissiveColor": (1.0, 0.40, 0.18)}),
	"MI_DB_Emissive_JukeboxStrips": ("flat", (0.6, 0.58, 0.5), 0.4, 0.04, {"Emissive": 200.0, "EmissiveColor": (1.0, 0.95, 0.85)}),
	"MI_DB_Emissive_LampBadge": ("flat", (0.5, 0.45, 0.3), 0.4, 0.04, {"Emissive": 150.0, "EmissiveColor": (1.0, 0.80, 0.55)}),
	"MI_DB_Emissive_LedRed": ("flat", (0.3, 0.02, 0.02), 0.3, 0.04, {"Emissive": 200.0, "EmissiveColor": (1.0, 0.05, 0.03)}),
	"MI_DB_Emissive_LedStrip": ("flat", (0.6, 0.6, 0.6), 0.3, 0.04, {"Emissive": 3000.0, "EmissiveColor": (1.0, 0.75, 0.50)}),
	"MI_DB_Felt_Green": ("fabric", (0.03, 0.09, 0.04), 0.95, 0.04, {}),
	"MI_DB_Foam_Exposed": ("fabric", (0.45, 0.38, 0.18), 0.95, 0.04, {}),
	"MI_DB_Glass_Amber": ("flat", (0.10, 0.045, 0.010), 0.05, 0.04, {}),
	"MI_DB_Glass_Ashtray": ("flat", (0.08, 0.09, 0.09), 0.05, 0.04, {}),
	"MI_DB_Glass_Green": ("flat", (0.015, 0.055, 0.022), 0.05, 0.04, {}),
	"MI_DB_Glass_Shelf": ("flat", (0.04, 0.05, 0.05), 0.04, 0.04, {}),
	"MI_DB_Label_Atlas": ("poster", (0.50, 0.45, 0.34), 0.60, 0.04, {}),
	"MI_DB_Label_Gloss": ("poster", (0.50, 0.45, 0.34), 0.30, 0.04, {}),
	"MI_DB_Liquid_Beer": ("flat", (0.30, 0.16, 0.03), 0.05, 0.04, {}),
	"MI_DB_Liquid_Clear": ("flat", (0.08, 0.08, 0.08), 0.05, 0.04, {}),
	"MI_DB_Liquid_Green": ("flat", (0.03, 0.12, 0.03), 0.05, 0.04, {}),
	"MI_DB_Liquid_Red": ("flat", (0.18, 0.02, 0.02), 0.05, 0.04, {}),
	"MI_DB_Liquid_Rum": ("flat", (0.20, 0.08, 0.02), 0.05, 0.04, {}),
	"MI_DB_Liquid_Whiskey": ("flat", (0.24, 0.10, 0.02), 0.05, 0.04, {}),
	"MI_DB_Mirror_Aged": ("mirror", (0.85, 0.85, 0.83), 0.03, 0.97, {}),
	"MI_DB_Plastic_Blue": ("flat", (0.02, 0.05, 0.20), 0.40, 0.04, {}),
	"MI_DB_Plastic_Red": ("flat", (0.25, 0.02, 0.02), 0.40, 0.04, {}),
	"MI_DB_Plastic_White": ("flat", (0.55, 0.54, 0.50), 0.40, 0.04, {}),
	"MI_DB_Plexi_Scratched": ("flat", (0.05, 0.05, 0.05), 0.15, 0.04, {}),
	"MI_DB_Tape": ("flat", (0.50, 0.44, 0.30), 0.60, 0.04, {}),
	"MI_DB_Tape_Electrical": ("flat", (0.015, 0.015, 0.015), 0.40, 0.04, {}),
	"MI_DB_Vinyl_Black": ("fabric", (0.015, 0.015, 0.016), 0.45, 0.04, {}),
	"MI_DB_Vinyl_Booth": ("fabric", (0.085, 0.016, 0.014), 0.45, 0.04, {}),
	"MI_DB_Wood_Ledge": ("wood", (0.080, 0.040, 0.018), 0.42, 0.04, {"AltColor": (0.036, 0.017, 0.008)}),
	"MI_DB_Neon_Dead": ("flat", (0.45, 0.45, 0.43), 0.12, 0.04, {}),
	"MI_DB_FX_DustMote": ("flat", (0.0, 0.0, 0.0), 1.0, 0.0, {}),
	"MI_DB_AxisTest_Body": ("flat", (0.50, 0.50, 0.50), 0.6, 0.04, {}),
	"MI_DB_AxisTest_Arrow": ("flat", (0.60, 0.05, 0.03), 0.5, 0.04, {}),
	"MI_DB_AxisTest_Marker": ("flat", (0.05, 0.40, 0.08), 0.5, 0.04, {}),
}

# Neon tubes (emissive, cd/m^2; venue-dive-bar 4.3): M2-B's MI_DB_Neon_<Gas> names, M2-A fallbacks until they exist.
NEON_GASES = {
	"ClearRed": (2160, (1.0, 0.10, 0.035)), "StdBlue": (2560, (0.16, 0.32, 1.0)), "RubyRed": (800, (1.0, 0.03, 0.08)),
	"CobaltBlue": (1600, (0.10, 0.16, 1.0)), "Green": (10026, (0.22, 1.0, 0.30)), "White": (6503, (1.0, 1.0, 1.0)),
	"NoviolGold": (6500, (1.0, 0.62, 0.16)),
}

# Greybox colours of layout.json "greybox_mat" (linear albedo, roughness, f0, pattern) - read-as-intended stand-ins, not final looks.
GREYBOX = {
	"dark_wood": ("wood", (0.060, 0.030, 0.014), 0.40, 0.04), "bar_top": ("wood", (0.150, 0.058, 0.024), 0.15, 0.05),
	"plank_wall": ("planks", (0.100, 0.052, 0.024), 0.50, 0.04), "mirror": ("mirror", (0.85, 0.85, 0.83), 0.03, 0.97),
	"poster": ("poster", (0.50, 0.45, 0.34), 0.78, 0.04), "photo": ("photo", (0.45, 0.40, 0.32), 0.35, 0.04),
	"polaroid": ("polaroid", (0.60, 0.57, 0.50), 0.40, 0.04), "frame_wood": ("wood", (0.050, 0.024, 0.011), 0.35, 0.04),
	"frame_black": ("flat", (0.015, 0.015, 0.015), 0.35, 0.04), "frame_gold": ("metal", (0.50, 0.38, 0.16), 0.35, 0.85),
	"enamel_black": ("enamel", (0.012, 0.012, 0.012), 0.30, 0.05), "cord": ("flat", (0.010, 0.010, 0.010), 0.55, 0.04),
	"sign_white": ("metal", (0.55, 0.53, 0.48), 0.40, 0.05), "sign_red": ("metal", (0.33, 0.02, 0.02), 0.40, 0.05),
	"glass": ("flat", (0.03, 0.04, 0.04), 0.05, 0.04), "cooler_body": ("metal", (0.03, 0.03, 0.03), 0.35, 0.05),
	"cooler_glass": ("flat", (0.04, 0.05, 0.05), 0.08, 0.04), "black_plastic": ("flat", (0.014, 0.014, 0.014), 0.40, 0.04),
	"black_rubber": ("flat", (0.016, 0.016, 0.016), 0.75, 0.04), "vinyl_oxblood": ("fabric", (0.085, 0.016, 0.014), 0.45, 0.04),
	"brass": ("flat", (0.91, 0.78, 0.42), 0.32, 0.95), "chrome": ("flat", (0.55, 0.56, 0.55), 0.10, 0.95),
	"red_paint": ("metal", (0.20, 0.018, 0.012), 0.35, 0.05), "popcorn_glass": ("flat", (0.06, 0.05, 0.03), 0.08, 0.04),
	"atm": ("metal", (0.06, 0.08, 0.12), 0.40, 0.05), "atm_screen": ("flat", (0.02, 0.03, 0.05), 0.08, 0.04),
	"pos_screen": ("flat", (0.02, 0.03, 0.04), 0.08, 0.04), "tv_screen": ("tv", (0.010, 0.010, 0.012), 0.06, 0.04),
	"black_steel": ("flat", (0.020, 0.020, 0.020), 0.45, 0.05), "rubber_mat": ("fabric", (0.014, 0.014, 0.014), 0.85, 0.04),
	"velour": ("fabric", (0.09, 0.012, 0.012), 0.95, 0.04), "radiator": ("metal", (0.20, 0.20, 0.19), 0.45, 0.30),
	"jar_glass": ("glow", (0.55, 0.57, 0.50), 0.40, 0.04), "frosted_glass": ("glow", (0.55, 0.50, 0.42), 0.40, 0.04), "clock_face": ("flat", (0.60, 0.58, 0.52), 0.30, 0.04),
	"paper": ("fabric", (0.55, 0.50, 0.40), 0.85, 0.04), "dart_cabinet": ("metal", (0.020, 0.020, 0.024), 0.35, 0.05),
	"dart_board": ("fabric", (0.02, 0.02, 0.02), 0.80, 0.04), "dart_marquee": ("flat", (0.30, 0.05, 0.04), 0.30, 0.04),
	"tape": ("flat", (0.55, 0.45, 0.10), 0.50, 0.04), "jukebox_body": ("metal", (0.05, 0.02, 0.05), 0.25, 0.05),
	"jukebox_panel": ("flat", (0.35, 0.12, 0.05), 0.25, 0.04), "jukebox_strips": ("flat", (0.60, 0.58, 0.50), 0.40, 0.04),
	"chalkboard": ("flat", (0.020, 0.026, 0.022), 0.90, 0.04), "stained_wood": ("wood", (0.080, 0.040, 0.018), 0.42, 0.04),
	"plywood_painted": ("flat", (0.040, 0.030, 0.024), 0.60, 0.04), "payphone": ("metal", (0.30, 0.30, 0.30), 0.40, 0.2),
	"troffer_lens": ("lens", (0.60, 0.60, 0.58), 0.30, 0.04), "diffuser": ("flat", (0.55, 0.52, 0.45), 0.45, 0.04),
	"sign_yellow": ("metal", (0.60, 0.42, 0.02), 0.40, 0.05), "bottle": ("flat", (0.06, 0.035, 0.010), 0.08, 0.04),
	"bottle_green": ("flat", (0.015, 0.05, 0.02), 0.08, 0.04), "bottle_clear": ("flat", (0.10, 0.10, 0.09), 0.06, 0.04),
	"label": ("fabric", (0.45, 0.40, 0.30), 0.60, 0.04), "felt": ("fabric", (0.04, 0.09, 0.05), 0.90, 0.04),
	"table_laminate": ("wood", (0.10, 0.055, 0.028), 0.35, 0.04), "enamel_green": ("enamel", (0.018, 0.055, 0.030), 0.28, 0.05),
	"enamel_white": ("enamel", (0.72, 0.70, 0.64), 0.25, 0.05), "chain": ("flat", (0.62, 0.62, 0.59), 0.42, 0.90),
	"stool_seat": ("fabric", (0.085, 0.016, 0.014), 0.45, 0.04), "stool_chrome": ("flat", (0.55, 0.56, 0.55), 0.12, 0.95),
	"booth_vinyl": ("fabric", (0.080, 0.015, 0.013), 0.45, 0.04), "cue_wood": ("wood", (0.30, 0.17, 0.08), 0.35, 0.04),
	"wood_light": ("wood", (0.28, 0.16, 0.08), 0.40, 0.04), "mug": ("flat", (0.55, 0.50, 0.42), 0.30, 0.04),
}

# Greybox parts that glow (luminance cd/m^2, colour): screens, panels, signs (venue-dive-bar 4.2 L11-L19, L31, L32).
GREYBOX_EMISSIVE = {
	"tv_screen": (80.0, (1.0, 1.0, 1.0)), "jukebox_panel": (120.0, (1.0, 0.45, 0.15)), "jukebox_strips": (200.0, (1.0, 0.95, 0.85)),
	"dart_marquee": (250.0, (1.0, 0.30, 0.25)), "atm_screen": (150.0, (0.70, 0.85, 1.0)), "pos_screen": (120.0, (0.60, 0.80, 1.0)),
	"clock_face": (100.0, (1.0, 0.85, 0.60)), "cooler_glass": (200.0, (0.85, 0.93, 1.0)), "popcorn_glass": (60.0, (1.0, 0.80, 0.50)),
	"frosted_glass": (450.0, (1.0, 0.66, 0.38)), "jar_glass": (500.0, (0.93, 1.0, 0.85)),
}

FALLBACK_HLSL = r"""
// M_DBA_Fallback (M2-A): procedural stand-in surfaces for the dive bar until M2-B's MI_DB_* exist. World-space patterns in metres.
float h31(float3 p) { p = frac(p * 0.1031); p += dot(p, p.yzx + 33.33); return frac((p.x + p.y) * p.z); }
float h21(float2 p) { return h31(float3(p, 17.13)); }
float vn(float3 p) {
	float3 i = floor(p); float3 f = frac(p); f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(lerp(h31(i), h31(i + float3(1,0,0)), f.x), lerp(h31(i + float3(0,1,0)), h31(i + float3(1,1,0)), f.x), f.y),
	            lerp(lerp(h31(i + float3(0,0,1)), h31(i + float3(1,0,1)), f.x), lerp(h31(i + float3(0,1,1)), h31(i + float3(1,1,1)), f.x), f.y), f.z);
}
float fbm(float3 p) { float a = 0.5, s = 0.0; for (int k = 0; k < 5; k++) { s += a * vn(p); p = p * 2.03 + 1.7; a *= 0.5; } return s; }
float2 plane_uv(float3 P, float3 N) {
	float3 a = abs(N);
	if (a.z > a.x && a.z > a.y) return P.xy;
	if (a.x > a.y) return float2(P.y, P.z);
	return float2(P.x, P.z);
}
// Height field [m] of a pattern at P (the bump of the surface normal).
float height(int pat, float3 P, float3 N, float Scale) {
	float2 uv = plane_uv(P, N);
	if (pat == 1) { float2 f = frac(uv / Scale); float2 e = min(f, 1.0 - f) * Scale; return -0.0004 * (1.0 - smoothstep(0.0, 0.0012, min(e.x, e.y))) + 0.00008 * vn(P * 90.0); }
	if (pat == 2) { float2 b = float2(uv.x / 0.2135, uv.y / 0.0675); b.x += 0.5 * fmod(floor(b.y), 2.0); float2 f = frac(b);
		float2 e = min(f, 1.0 - f) * float2(0.2135, 0.0675); float m = 1.0 - smoothstep(0.003, 0.007, min(e.x, e.y));
		return -0.004 * m + 0.0007 * fbm(P * 30.0) - 0.0012 * step(0.86, fbm(P * 4.0 + 4.0) + 0.12 * saturate(1.0 - P.z / 1.2)); }
	if (pat == 3) { return -0.0012 * step(0.80, vn(P * 110.0)) - 0.0008 * step(0.84, vn(P * 47.0 + 9.0)) + 0.0003 * fbm(P * 20.0); }
	if (pat == 5) { float2 f = frac(uv / 0.203); return 0.0015 * cos(f.x * 6.2832 * 5.0) + 0.0008 * sin((f.y + 0.1 * sin(f.x * 12.0)) * 6.2832 * 3.0); }
	if (pat == 6) { return 0.00025 * vn(P * 140.0) + 0.0002 * fbm(P * 11.0); }
	if (pat == 7) { float2 b = float2(uv.x / 0.406, uv.y / 0.203); b.x += 0.5 * fmod(floor(b.y), 2.0); float2 f = frac(b);
		float2 e = min(f, 1.0 - f) * float2(0.406, 0.203); return -0.003 * (1.0 - smoothstep(0.003, 0.008, min(e.x, e.y))) + 0.0006 * vn(P * 60.0); }
	if (pat == 8) { float2 f = frac(uv / 0.3048); float2 e = min(f, 1.0 - f); return 0.004 * smoothstep(0.05, 0.08, min(e.x, e.y)) + 0.002 * (1.0 - smoothstep(0.02, 0.04, abs(length(f - 0.5) - 0.2))); }
	if (pat == 9) { float fb = frac(uv.y / 0.14); return -0.003 * (1.0 - smoothstep(0.02, 0.05, min(fb, 1.0 - fb))) + 0.0004 * vn(float3(uv.x * 2.0, uv.y * 40.0, 0.0)); }
	if (pat == 12) { float g = frac(uv.x / 0.2032 + 0.37 * step(0.5, frac(uv.x / 1.22))); float e = min(g, 1.0 - g) * 0.2032;
		return -0.0025 * (1.0 - smoothstep(0.001, 0.004, e)) + 0.00015 * vn(float3(uv.x * 30.0, uv.y * 1.5, 0.0)); }
	if (pat == 10 || pat == 18) { return -0.0005 * step(0.72, fbm(P * 9.0)); }
	if (pat == 15 || pat == 16) { return 0.0006 * fbm(P * 25.0); }
	if (pat == 17) { return 0.0002 * vn(P * 300.0); }
	return 0.0;
}
"""

FALLBACK_BODY = r"""
float3 P = WorldPos / 100.0;              // cm -> m
float3 N = normalize(VertexNormal);
float2 uv = plane_uv(P, N);
float3 albedo = BaseColor;
float rough = BaseRough;
float3 emis = 0;
float grime = fbm(P * 1.7);
float fine = vn(P * 37.0);
int pat = (int)round(Pattern);
if (pat == 1) {                           // VCT 12 in tiles, checker, per-tile tone, waxed edges / dull traffic lanes, sticky patches
	float2 t = floor(uv / Scale);
	float chk = fmod(abs(t.x + t.y), 2.0);
	float3 c = lerp(BaseColor, AltColor, chk);
	float ht = h21(t);
	c *= 0.90 + 0.18 * ht;
	float patch = (Variant < 0.5 && uv.x > 5.18 && uv.x < 6.10 && uv.y > 2.44 && uv.y < 3.36) ? 1.0 : 0.0;   // 9 replaced tiles
	c = lerp(c, float3(0.21, 0.050, 0.035), patch * (1.0 - chk));
	float lane = saturate(fbm(float3(uv * 0.55, 3.0)) * 1.7 - 0.55);
	float sticky = saturate(fbm(float3(uv * 1.3, 7.0)) * 2.2 - 1.25);
	albedo = c * lerp(1.0, 0.75, lane * grime) * (1.0 - 0.25 * sticky);
	rough = lerp(0.30, 0.62, lane) + (ht - 0.5) * 0.08 + (fine - 0.5) * 0.06;
	rough = lerp(rough, 0.40, sticky);
} else if (pat == 2) {                    // painted brick, running bond 203 x 57 + 10 mm joints, chipped paint
	float2 b = float2(uv.x / 0.2135, uv.y / 0.0675);
	float row = floor(b.y); b.x += 0.5 * fmod(row, 2.0);
	float2 f = frac(b);
	float2 e = min(f, 1.0 - f) * float2(0.2135, 0.0675);
	float mortar = 1.0 - smoothstep(0.003, 0.006, min(e.x, e.y));
	float chip = step(0.86, fbm(P * 4.0 + 4.0) + 0.18 * vn(P * 40.0) + 0.12 * saturate(1.0 - P.z / 1.2));
	float3 brick = AltColor * (0.75 + 0.5 * h21(floor(b)));
	float3 paint = BaseColor * (0.85 + 0.25 * fine) * (0.9 + 0.2 * h21(floor(b) + 3.0));
	albedo = lerp(paint, brick, chip * (1.0 - mortar));
	albedo = lerp(albedo, BaseColor * 0.6, mortar * 0.6);
	rough = lerp(BaseRough, 0.95, chip);
} else if (pat == 3) {                    // fissured mineral ceiling tile, nicotine yellowed (stronger over the bar), water rings
	float pits = step(0.80, vn(P * 110.0)) * 0.6 + step(0.84, vn(P * 47.0 + 9.0)) * 0.4;
	float2 tsz = (Variant > 0.5) ? float2(0.61, 0.61) : float2(1.22, 0.61);
	float2 tile = floor(P.xy / tsz);
	float2 tf = frac(P.xy / tsz);
	float th = h21(tile + 0.37);
	albedo = BaseColor * (0.92 + 0.12 * fbm(P * 3.0)) * (1.0 - 0.35 * pits) * (0.84 + 0.26 * th);
	// 60 years of smoke (S5): nicotine-yellow to brown, heaviest over the bar and near the front, per tile uneven; a few newer tiles
	float nic = saturate(0.55 + 0.45 * saturate(1.0 - P.x / 11.0) + 0.35 * saturate(fbm(P * 0.5) * 1.4 - 0.4) + 0.2 * (th - 0.5));
	float newer = step(0.94, th);
	albedo *= lerp(float3(1, 1, 1), float3(0.68, 0.53, 0.30), nic * Age * (1.0 - 0.8 * newer));
	albedo *= lerp(1.0, 0.82, saturate(1.0 - min(min(tf.x, 1.0 - tf.x) * tsz.x, min(tf.y, 1.0 - tf.y) * tsz.y) / 0.06) * Age);
	// water stains: brown tide-marked blotches on some tiles (the roof leaks over the pool room corner too)
	float ws = step(0.82, h21(tile + 5.3));
	float2 wc = tf - 0.5 - 0.25 * float2(h21(tile + 2.1) - 0.5, h21(tile + 8.7) - 0.5);
	float wr = length(wc * tsz) / (0.12 + 0.18 * h21(tile + 4.4)) + 0.25 * fbm(P * 6.0);
	float blotch = ws * (1.0 - smoothstep(0.85, 1.0, wr));
	float tide = ws * (1.0 - smoothstep(0.0, 0.06, abs(wr - 0.95)));
	albedo *= lerp(float3(1, 1, 1), float3(0.62, 0.48, 0.30), 0.6 * blotch);
	albedo *= 1.0 - 0.35 * tide;
	rough = 0.94;
} else if (pat == 4) {                    // stained / lacquered wood grain
	float2 w = float2(uv.x * 6.0, uv.y * 0.35);
	float ring = frac(fbm(float3(w, 0.0)) * 7.0 + uv.x * 3.0);
	float grain = smoothstep(0.35, 0.65, ring);
	albedo = lerp(BaseColor, AltColor, grain * 0.8) * (0.85 + 0.25 * vn(float3(uv.x * 60.0, uv.y * 2.0, 0.0)));
	rough = BaseRough + 0.10 * grain + 0.15 * saturate(grime - 0.55);
} else if (pat == 5) {                    // glass block 203 mm, lit from the street behind
	float2 f = frac(uv / 0.203);
	float2 e = min(f, 1.0 - f);
	float flute = 0.5 + 0.5 * cos(f.x * 6.2831 * 5.0);
	float wave = 0.5 + 0.5 * sin((f.y + 0.1 * sin(f.x * 12.0)) * 6.2831 * 3.0);
	float glow = smoothstep(0.0, 0.25, min(e.x, e.y)) * (0.45 + 0.35 * flute + 0.2 * wave) * (0.55 + 0.45 * fbm(float3(uv * 1.5, 1.0)));
	float street = saturate(1.3 - abs(uv.y - 1.8) * 0.9) * (0.7 + 0.3 * saturate(1.0 - abs(uv.x - 2.2) * 0.3));
	albedo = BaseColor;
	rough = 0.06;
	emis = EmissiveColor * Emissive * glow * street;
} else if (pat == 6) {                    // painted plaster: roller stipple, stains, nicotine above the bar
	albedo = BaseColor * (0.92 + 0.08 * vn(P * 25.0) + 0.10 * fbm(P * 0.8));
	albedo *= lerp(1.0, 0.80, saturate(fbm(P * 1.3 + 2.0) - 0.55) * 2.0);
	albedo *= lerp(float3(1,1,1), float3(0.85, 0.75, 0.55), Variant * Age * saturate((P.z - 1.4) / 1.3));
	rough = BaseRough;
} else if (pat == 7) {                    // painted concrete block 406 x 203
	float2 b = float2(uv.x / 0.406, uv.y / 0.203); b.x += 0.5 * fmod(floor(b.y), 2.0);
	albedo = BaseColor * (0.88 + 0.12 * vn(P * 30.0)) * (0.94 + 0.08 * h21(floor(b)));
	rough = BaseRough;
} else if (pat == 8) {                    // pressed tin: raised squares, rust at the edges
	float2 f = frac(uv / 0.3048);
	float2 e = min(f, 1.0 - f);
	float ridge = step(min(e.x, e.y), 0.06) + step(abs(length(f - 0.5) - 0.2), 0.02);
	albedo = lerp(BaseColor, AltColor, saturate(ridge * step(0.55, fbm(P * 8.0)))) * (0.75 + 0.3 * fine);
	rough = BaseRough;
} else if (pat == 9) {                    // plank wall (bar die): horizontal boards 140 mm, kick grime
	float board = floor(uv.y / 0.14);
	float2 w = float2(uv.x * 0.4, uv.y * 8.0 + h21(float2(board, 3.0)) * 10.0);
	float grain = smoothstep(0.3, 0.7, frac(fbm(float3(w, board)) * 6.0));
	albedo = lerp(BaseColor, AltColor, grain * 0.7) * (0.8 + 0.35 * h21(float2(board, 1.0)));
	float kick = saturate(1.0 - P.z / 0.30) * grime;
	albedo *= 1.0 - 0.5 * kick;
	rough = BaseRough + 0.2 * kick;
} else if (pat == 10) {                   // painted steel: chips to rust at kick height
	float chip = step(0.72, fbm(P * 9.0) + 0.3 * saturate(1.0 - P.z / 0.4));
	albedo = lerp(BaseColor * (0.9 + 0.2 * fine), AltColor, chip);
	rough = lerp(BaseRough, 0.8, chip);
} else if (pat == 11) {                   // night street across Harbor St. (backplate): facades, lit windows, sodium lamp, wet reflection
	float y = uv.x; float z = P.z;
	float3 sky = lerp(float3(0.010, 0.012, 0.020), float3(0.030, 0.022, 0.015), saturate(1.0 - (z - 3.0) / 3.0));
	float bld = step(z, 4.6 + 0.8 * step(0.5, h21(float2(floor(y / 3.1), 1.0))));
	float2 wc = float2(y / 1.3, (z - 1.2) / 1.6);
	float2 wf = frac(wc);
	float win = step(0.2, wf.x) * step(wf.x, 0.8) * step(0.25, wf.y) * step(wf.y, 0.85) * step(1.0, wc.y) * step(0.62, h21(floor(wc)));
	float3 wcol = lerp(float3(1.0, 0.72, 0.40), float3(0.55, 0.65, 1.0), step(0.75, h21(floor(wc) + 5.0)));
	float3 facade = float3(0.004, 0.0035, 0.003) * (0.7 + 0.6 * fbm(P * 2.0));
	float3 c = lerp(sky, facade, bld) + win * bld * wcol * 1.4;
	float lamp = exp(-dot(float2(y - 1.8, z - 4.1), float2(y - 1.8, z - 4.1)) / 0.35);
	float lamp2 = exp(-dot(float2(y - 9.5, z - 4.1), float2(y - 9.5, z - 4.1)) / 0.35);
	float3 sodium = float3(1.0, 0.45, 0.10);
	c += sodium * (lamp + lamp2) * 25.0 + sodium * 0.5 * saturate(1.0 - abs(z - 0.2) / 0.8) * (lamp * 0 + 1.0) * 0.4;
	albedo = 0;
	rough = 0.9;
	emis = c * Emissive;
} else if (pat == 12) {                   // 1970s grooved paneling (random-plank V grooves, dark stain)
	float plank = floor(uv.x / 0.2032);
	float2 w = float2(uv.x * 5.0, uv.y * 0.30 + h21(float2(plank, 2.0)) * 7.0);
	float ring = frac(fbm(float3(w, plank)) * 6.0 + uv.x * 2.0);
	float grain = smoothstep(0.3, 0.7, ring);
	float g = frac(uv.x / 0.2032 + 0.37 * step(0.5, frac(uv.x / 1.22)));
	float groove = 1.0 - smoothstep(0.001, 0.004, min(g, 1.0 - g) * 0.2032);
	albedo = lerp(BaseColor, AltColor, grain * 0.75) * (0.8 + 0.35 * h21(float2(plank, 7.0))) * (1.0 - 0.6 * groove);
	rough = BaseRough + 0.1 * grain + 0.2 * groove;
} else if (pat == 13) {                   // troffer prismatic lens (emissive driven by the light's state ramp)
	float2 f = frac(uv / 0.004);
	albedo = BaseColor * (0.9 + 0.1 * step(0.5, f.x));
	rough = 0.3;
	emis = EmissiveColor * Emissive * (0.92 + 0.08 * vn(P * 8.0));
} else if (pat == 14) {                   // wired glass (diamond wire grid 25 mm)
	float2 d = float2(uv.x + uv.y, uv.x - uv.y) / 0.025;
	float2 f = frac(d);
	float wire = step(min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)), 0.04);
	albedo = lerp(BaseColor, float3(0.25, 0.25, 0.24), wire);
	rough = lerp(0.05, 0.4, wire);
} else if (pat == 15) {                   // wet asphalt
	albedo = BaseColor * (0.8 + 0.4 * fbm(P * 6.0));
	rough = lerp(0.08, 0.45, saturate(fbm(P * 0.8) * 1.6 - 0.4));
} else if (pat == 16) {                   // concrete / mortar
	albedo = BaseColor * (0.85 + 0.25 * fbm(P * 8.0));
	rough = BaseRough;
} else if (pat == 17) {                   // fabric / vinyl / rubber (fine texture, a little sheen)
	albedo = BaseColor * (0.9 + 0.15 * vn(P * 200.0) + 0.1 * fbm(P * 5.0));
	rough = BaseRough;
} else if (pat == 18) {                   // enamel with chips to the substrate
	float chip = step(0.78, fbm(P * 14.0));
	albedo = lerp(BaseColor * (0.95 + 0.1 * fine), AltColor, chip);
	rough = lerp(BaseRough, 0.7, chip);
} else if (pat >= 19 && pat <= 24) {    // content surfaces in the object's own frame: q = [0, 1]^2 across the face, oseed per object
	float3 Lc = (WorldPos - ObjPos) / max(ObjSize, float3(0.1, 0.1, 0.1)) + 0.5;
	float3 an = abs(N);
	float2 q = (an.x > an.y && an.x > an.z) ? float2(N.x > 0.0 ? 1.0 - Lc.y : Lc.y, Lc.z) : ((an.y > an.z) ? float2(N.y > 0.0 ? Lc.x : 1.0 - Lc.x, Lc.z) : Lc.xy);
	float oseed = h31(floor(ObjPos * 0.13) + 11.0);
	if (pat == 19) {                      // TV: a night game on a sports channel (grass, lines, crowd band, players, score bug, logo)
		float3 c = float3(0.09, 0.30, 0.06) * (0.80 + 0.20 * step(0.5, frac(q.x * 7.0 + 0.25 * q.y))) * (0.75 + 0.35 * q.y);
		float crowd = smoothstep(0.70, 0.72, q.y);
		float2 cc = floor(q * float2(220.0, 60.0));
		float3 crowdc = float3(0.05, 0.045, 0.06) + 0.35 * float3(h21(cc), h21(cc + 7.1), h21(cc + 3.3)) * step(0.55, h21(cc + 1.9));
		c = lerp(c, crowdc, crowd);
		float ln = (1.0 - crowd) * (step(abs(frac(q.x * 2.5 + 0.1 * q.y) - 0.5), 0.006) + step(abs(q.y - 0.18), 0.004));
		c = lerp(c, float3(0.80, 0.80, 0.76), saturate(ln));
		for (int k = 0; k < 10; k++) {
			float2 pp = float2(0.08 + 0.84 * h21(float2(k, oseed * 13.0)), 0.25 + 0.38 * h21(float2(k + 21, oseed * 7.0)));
			float body = step(length((q - pp) * float2(1.0, 0.45)), 0.012);
			c = lerp(c, (k < 5) ? float3(0.70, 0.08, 0.06) : float3(0.85, 0.85, 0.85), body);
		}
		float bug = step(0.04, q.x) * step(q.x, 0.40) * step(0.05, q.y) * step(q.y, 0.13);
		c = lerp(c, float3(0.02, 0.03, 0.09), bug);
		float2 tc = floor(q * float2(120.0, 26.0));
		c = lerp(c, float3(0.92, 0.92, 0.92), bug * step(0.45, h21(tc)) * step(abs(frac(q.y * 26.0) - 0.5), 0.28));
		c = lerp(c, float3(0.9, 0.75, 0.1), 0.8 * step(0.86, q.x) * step(q.x, 0.95) * step(0.84, q.y) * step(q.y, 0.93));
		albedo = BaseColor;
		rough = 0.05;
		emis = c * (Emissive / 0.16) * EmissiveColor;
	} else if (pat == 20) {               // poster / flyer: background colour, title letters, a picture, small print, aged paper
		float s = oseed;
		float3 bgs[5] = { float3(0.52, 0.46, 0.33), float3(0.020, 0.020, 0.022), float3(0.30, 0.025, 0.020), float3(0.58, 0.42, 0.04), float3(0.030, 0.07, 0.22) };
		int bi = (int)floor(s * 4.999);
		float3 bg = bgs[bi];
		float3 ink = (bi == 0 || bi == 3) ? float3(0.03, 0.03, 0.03) : float3(0.62, 0.58, 0.48);
		float3 acc = (h21(float2(s, 2.0)) > 0.5) ? float3(0.45, 0.06, 0.03) : float3(0.05, 0.12, 0.30);
		float margin = step(0.06, q.x) * step(q.x, 0.94);
		float tl = step(0.74, q.y) * step(q.y, 0.90) * margin;
		float cell = frac(q.x * 11.0);
		float letters = step(0.40, vn(float3(floor(q.x * 11.0 + s * 5.0) + 0.5, floor(q.y * 12.0), s * 9.0))) * step(0.12, cell) * step(cell, 0.88);
		float2 pc = q - float2(0.5, 0.50);
		float pic = (h21(float2(s, 5.0)) > 0.5) ? step(length(pc * float2(1.0, 1.3)), 0.20) : step(abs(pc.x), 0.32) * step(abs(pc.y), 0.15);
		float3 picc = lerp(acc, ink, 0.25 * vn(float3(q * 18.0, s)));
		float sp = step(0.08, q.y) * step(q.y, 0.28) * margin * step(abs(frac(q.y * 22.0) - 0.5), 0.22) * step(0.30, vn(float3(q.x * 40.0, floor(q.y * 22.0), s * 3.0)));
		float3 c = lerp(bg, picc, pic);
		c = lerp(c, ink, saturate(tl * letters + sp));
		float edge = 1.0 - smoothstep(0.0, 0.05, min(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y)));
		c *= lerp(float3(1, 1, 1), float3(0.85, 0.74, 0.52), Age * 0.6);
		c *= (1.0 - 0.45 * edge * Age) * (0.85 + 0.25 * fbm(float3(q * 6.0, s * 4.0)));
		albedo = c;
		rough = 0.80;
	} else if (pat == 21) {               // framed photo: faded print on a mat, figures, vignette, sepia or washed-out colour
		float s = oseed;
		float matb = 1.0 - step(0.08, q.x) * step(q.x, 0.92) * step(0.08, q.y) * step(q.y, 0.92);
		float3 c = lerp(float3(0.30, 0.26, 0.20), float3(0.12, 0.10, 0.08), q.y) * (0.8 + 0.4 * fbm(float3(q * 3.0, s * 7.0)));
		for (int k = 0; k < 5; k++) {
			float px = 0.18 + 0.64 * h21(float2(k, s * 11.0));
			float fig = 1.0 - smoothstep(0.035, 0.05, length((q - float2(px, 0.35)) * float2(1.0, 0.38)));
			float head = 1.0 - smoothstep(0.022, 0.03, length(q - float2(px, 0.62)));
			c = lerp(c, float3(0.06, 0.05, 0.04) + 0.1 * h21(float2(k, s)), saturate(fig + head));
		}
		float lum = dot(c, float3(0.3, 0.59, 0.11));
		c = lerp(c, lum * float3(1.15, 0.95, 0.70), 0.5 + 0.5 * step(0.5, h21(float2(s, 9.0))));
		c *= 1.0 - 0.5 * saturate(length(q - 0.5) * 1.3 - 0.35);
		c = lerp(c, float3(0.55, 0.50, 0.40), matb);
		c *= lerp(float3(1, 1, 1), float3(0.9, 0.8, 0.6), 0.4 * Age);
		albedo = c;
		rough = 0.25 + 0.3 * matb;
	} else if (pat == 22) {               // Polaroid: white border (thick at the bottom), a flash-lit night snapshot, faded
		float s = oseed;
		float img = step(0.07, q.x) * step(q.x, 0.93) * step(0.22, q.y) * step(q.y, 0.93);
		float2 im = float2((q.x - 0.07) / 0.86, (q.y - 0.22) / 0.71);
		float3 c = lerp(float3(0.25, 0.12, 0.05), float3(0.05, 0.04, 0.05), im.y) * (0.7 + 0.6 * fbm(float3(im * 4.0, s * 9.0)));
		for (int k = 0; k < 3; k++) {
			float2 fc = float2(0.2 + 0.6 * h21(float2(k, s * 5.0)), 0.45 + 0.15 * h21(float2(k + 3, s * 5.0)));
			float face = 1.0 - smoothstep(0.06, 0.09, length((im - fc) * float2(1.0, 0.8)));
			float body = 1.0 - smoothstep(0.10, 0.14, length((im - fc + float2(0.0, 0.3)) * float2(0.8, 1.0)));
			c = lerp(c, float3(0.08, 0.07, 0.12) + 0.3 * h21(float2(k, s)) * float3(0.6, 0.1, 0.1), body * (1.0 - face));
			c = lerp(c, float3(0.55, 0.38, 0.28), face);
		}
		c = lerp(float3(0.62, 0.60, 0.54), c, img);
		c *= lerp(float3(1, 1, 1), float3(0.92, 0.82, 0.62), 0.5 * Age);
		albedo = c;
		rough = 0.30;
	} else if (pat == 23) {               // frosted glass with a lamp inside: brightest at the bulb's height, dust on top
		albedo = BaseColor * (0.9 + 0.1 * fine);
		rough = 0.45;
		emis = EmissiveColor * Emissive * (1.0 - 0.55 * saturate(abs(Lc.z - 0.45) * 2.0)) * (0.85 + 0.15 * fbm(P * 40.0));
	} else {                              // aged back-bar mirror: desilvering at the edges and in blotches, grime
		float edge = 1.0 - smoothstep(0.0, 0.06, min(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y)));
		float blot = saturate((fbm(float3(uv * 9.0, 5.0)) - 0.52) * 5.0) * edge + 0.7 * step(0.965, vn(P * 30.0)) + 0.25 * edge * step(0.5, fbm(float3(uv * 25.0, 2.0)));
		albedo = lerp(BaseColor, float3(0.10, 0.09, 0.08), saturate(blot));
		rough = lerp(0.03, 0.35, saturate(blot)) + 0.05 * fbm(P * 20.0);
	}
} else {
	albedo = BaseColor * (0.92 + 0.12 * fbm(P * 2.0));
	emis = EmissiveColor * Emissive;
}
// Venue-wide wear (Age 0.80): kick grime near the floor and dust on up-facing surfaces (6.2, cavity-less approximation).
if (pat != 11 && pat != 13 && pat != 19) {
	float kickg = Age * saturate(1.0 - P.z / 0.30) * saturate(grime * 1.4 - 0.3);
	albedo *= 1.0 - 0.45 * kickg;
	float dust = Age * DustAmount * saturate((N.z - 0.6) / 0.4) * saturate(fbm(P * 4.0) * 1.3) * step(0.05, P.z);
	albedo = lerp(albedo, float3(0.35, 0.33, 0.30) * 0.5, 0.5 * dust);
	rough = saturate(lerp(rough, 0.9, dust));
}
// Bump: finite differences of the pattern height along the surface (1 mm), world-space normal (the material has
// "tangent space normal" off).
float3 T1 = normalize(abs(N.z) < 0.9 ? cross(N, float3(0, 0, 1)) : cross(N, float3(1, 0, 0)));
float3 T2 = cross(N, T1);
float e = 0.001;
float h0 = height(pat, P, N, Scale);
float hx = height(pat, P + T1 * e, N, Scale);
float hy = height(pat, P + T2 * e, N, Scale);
NormalWS = normalize(N - ((hx - h0) / e) * T1 - ((hy - h0) / e) * T2);
Roughness = saturate(rough);
Emis = emis * EmissiveScale;
return saturate(albedo);
"""


def _mel():
	return unreal.MaterialEditingLibrary


FALLBACK_VERSION = 6  # bump to rebuild the master (stored in the material's metadata with a hash of the HLSL: any code change rebuilds too)


def _fallback_tag() -> str:
	import hashlib  # noqa: PLC0415
	return f"RbDBFallback v{FALLBACK_VERSION} " + hashlib.sha1((FALLBACK_HLSL + FALLBACK_BODY).encode("utf-8")).hexdigest()[:12]


def make_fallback_master():
	"""M_DBA_Fallback: Substrate slab driven by one inline Custom node (FALLBACK_HLSL). Built once per import run (per version)."""
	if unreal.EditorAssetLibrary.does_asset_exist(FALLBACK_MASTER):
		m = unreal.load_asset(FALLBACK_MASTER)
		try:
			if _fallback_tag() == str(unreal.EditorAssetLibrary.get_metadata_tag(m, "RbFallbackVersion") or ""):
				return m
		except Exception:  # noqa: BLE001
			pass
		unreal.EditorAssetLibrary.delete_asset(FALLBACK_MASTER)
	_ensure_dir(FALLBACK_DIR)
	mel = _mel()
	tools = unreal.AssetToolsHelpers.get_asset_tools()
	m = tools.create_asset("M_DBA_Fallback", FALLBACK_DIR, unreal.Material, unreal.MaterialFactoryNew())
	m.set_editor_property("used_with_nanite", True)
	m.set_editor_property("used_with_instanced_static_meshes", True)
	m.set_editor_property("tangent_space_normal", False)
	y = [0]

	def node(cls, x=-800, **props):
		y[0] += 110
		e = mel.create_material_expression(m, cls, x, y[0])
		for k, v in props.items():
			e.set_editor_property(k, v)
		return e

	def scalar(name, value):
		return node(unreal.MaterialExpressionScalarParameter, -1400, parameter_name=name, default_value=float(value), group="Fallback")

	def vector(name, rgb):
		return node(unreal.MaterialExpressionVectorParameter, -1400, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), group="Fallback")

	world = node(unreal.MaterialExpressionWorldPosition, -1400)
	normal = node(unreal.MaterialExpressionVertexNormalWS, -1400)
	obj_pos = node(unreal.MaterialExpressionObjectPositionWS, -1400)
	obj_size = node(unreal.MaterialExpressionObjectBounds, -1400)
	inputs = [("WorldPos", world, ""), ("VertexNormal", normal, ""), ("ObjPos", obj_pos, ""), ("ObjSize", obj_size, ""),
		("BaseColor", vector("BaseColor", (0.5, 0.5, 0.5)), "RGB"),
		("AltColor", vector("AltColor", (0.2, 0.2, 0.2)), "RGB"), ("BaseRough", scalar("Roughness", 0.6), ""), ("Pattern", scalar("Pattern", 0), ""),
		("Scale", scalar("Scale", 0.305), ""), ("Emissive", scalar("Emissive", 0.0), ""), ("EmissiveColor", vector("EmissiveColor", (1, 1, 1)), "RGB"),
		("Age", scalar("Age", 0.8), ""), ("DustAmount", scalar("DustAmount", 0.6), ""), ("Variant", scalar("Variant", 0.0), ""),
		("EmissiveScale", scalar("EmissiveScale", emissive_scale()), "")]
	custom = node(unreal.MaterialExpressionCustom, -500, description="RbDBFallback", output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
	# A Custom node's code is a function body; helper functions live in a local struct (the usual Custom-node idiom: no shader
	# include is needed, and M2-A owns no shader folder).
	code = "struct RbF {\n" + FALLBACK_HLSL + "\n};\nRbF F;\n"
	body = FALLBACK_BODY
	for fn in ("h31(", "h21(", "vn(", "fbm(", "plane_uv(", "height("):
		body = body.replace(fn, "F." + fn)
	body = body.replace("F.F.", "F.")
	custom.set_editor_property("code", code + body)
	cis = []
	for name, _, _ in inputs:
		ci = unreal.CustomInput()
		ci.set_editor_property("input_name", name)
		cis.append(ci)
	custom.set_editor_property("inputs", cis)
	outs = []
	for name, typ in (("Roughness", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("Emis", unreal.CustomMaterialOutputType.CMOT_FLOAT3),
			("NormalWS", unreal.CustomMaterialOutputType.CMOT_FLOAT3)):
		co = unreal.CustomOutput()
		co.set_editor_property("output_name", name)
		co.set_editor_property("output_type", typ)
		outs.append(co)
	custom.set_editor_property("additional_outputs", outs)
	for name, expression, pin in inputs:
		if not mel.connect_material_expressions(expression, pin, custom, name):
			_fail(f"M_DBA_Fallback: could not connect {name}")
	f0 = scalar("F0", 0.04)
	slab = node(unreal.MaterialExpressionSubstrateSlabBSDF, -100)
	if not mel.connect_material_expressions(custom, "", slab, "Diffuse Albedo"):
		mel.connect_material_expressions(custom, "", slab, "DiffuseAlbedo")
	for pin_from, pin_to in (("Roughness", "Roughness"), ("Emis", "Emissive Color"), ("NormalWS", "Normal")):
		if not mel.connect_material_expressions(custom, pin_from, slab, pin_to):
			if not mel.connect_material_expressions(custom, pin_from, slab, pin_to.replace(" ", "")):
				_fail(f"M_DBA_Fallback: could not connect {pin_from} -> {pin_to}")
	mel.connect_material_expressions(f0, "", slab, "F0")
	if not mel.connect_material_property(slab, "", unreal.MaterialProperty.MP_FRONT_MATERIAL):
		_fail("M_DBA_Fallback: front material")
	mel.layout_material_expressions(m)
	mel.recompile_material(m)
	unreal.EditorAssetLibrary.set_metadata_tag(m, "RbFallbackVersion", _fallback_tag())
	unreal.EditorAssetLibrary.save_loaded_asset(m, False)
	_log(f"created M_DBA_Fallback ({_fallback_tag()})")
	return m


def make_instance(folder: str, name: str, parent, scalars: dict, vectors: dict):
	path = f"{folder}/{name}"
	mel = _mel()
	mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
	if mi is None:
		_ensure_dir(folder)
		mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
	mel.set_material_instance_parent(mi, parent)
	mel.clear_all_material_instance_parameters(mi)
	scalars = dict(scalars)
	if parent is not None and parent.get_path_name().startswith(FALLBACK_MASTER):
		scalars.setdefault("EmissiveScale", emissive_scale())  # Art/DiveBar/calibration.json (6.1)
	for k, v in scalars.items():
		mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
	for k, v in vectors.items():
		mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
	mel.update_material_instance(mi)
	unreal.EditorAssetLibrary.save_loaded_asset(mi, False)
	return mi


_FALLBACK_CACHE: dict = {}


def fallback_material(slot: str):
	"""An M2-A fallback instance for a slot with no real material yet (None when the slot is unknown)."""
	if slot in _FALLBACK_CACHE:
		return _FALLBACK_CACHE[slot]
	master = make_fallback_master()
	mi = None
	if slot.startswith("MI_DB_Neon_") and slot[len("MI_DB_Neon_"):] in NEON_GASES:
		nits, color = NEON_GASES[slot[len("MI_DB_Neon_"):]]
		mi = make_instance(FALLBACK_DIR, slot.replace("MI_DB_", "MI_DBA_"), master, {"Pattern": 0, "Roughness": 0.15, "Emissive": nits,
			"DustAmount": 0.0}, {"BaseColor": (0.02, 0.02, 0.02), "EmissiveColor": color})
	elif slot.startswith("MI_DB_GB_") and slot[len("MI_DB_GB_"):] in GREYBOX:
		key = slot[len("MI_DB_GB_"):]
		pattern, albedo, rough, f0 = GREYBOX[key]
		scalars = {"Pattern": PATTERN[pattern], "Roughness": rough, "F0": f0}
		vectors = {"BaseColor": albedo, "AltColor": tuple(0.5 * c for c in albedo)}
		if key in GREYBOX_EMISSIVE:
			scalars["Emissive"], vectors["EmissiveColor"] = GREYBOX_EMISSIVE[key]
			scalars["Pattern"] = PATTERN["flat"] if pattern == "flat" else scalars["Pattern"]
		mi = make_instance(FALLBACK_DIR + "/Greybox", slot.replace("MI_DB_", "MI_DBA_"), master, scalars, vectors)
	else:
		spec = FALLBACK_SURFACES.get(slot)
		if spec is not None:
			pattern, albedo, rough, f0, extra = spec
			scalars = {"Pattern": PATTERN[pattern], "Roughness": rough, "F0": f0}
			vectors = {"BaseColor": albedo}
			for k, v in extra.items():
				(vectors if isinstance(v, tuple) else scalars)[k] = v
			mi = make_instance(FALLBACK_DIR, slot.replace("MI_DB_", "MI_DBA_"), master, scalars, vectors)
	_FALLBACK_CACHE[slot] = mi
	return mi


def emissive_scale() -> float:
	"""MPC_DB_Venue.EmissiveScale source (Art/DiveBar/calibration.json; 1.0 when missing, venue-dive-bar 6.1)."""
	path = os.path.join(REPO, "Art", "DiveBar", "calibration.json")
	if os.path.exists(path):
		with open(path, "r", encoding="utf-8") as handle:
			return float(json.load(handle).get("EmissiveScale", 1.0))
	return 1.0


def _b_materials_module():
	"""M2-B's rb_make_divebar_materials (its asset_materials hook), or None while it is a stub / missing."""
	try:
		editor_dir = os.path.join(REPO, "Tools", "unreal", "editor")
		if editor_dir not in sys.path:
			sys.path.insert(0, editor_dir)
		import rb_make_divebar_materials as bmat  # noqa: PLC0415
		return bmat if hasattr(bmat, "asset_materials") else None
	except Exception:  # noqa: BLE001 - the stub may log / raise on import
		return None


def resolve_materials(meta: dict, json_path: str) -> dict:
	"""slot -> (material, kind) for every slot of an asset: M2-B's per-asset hook when every slot has an M2-B instance, else per slot
	M2-B's MI_DB_*, M2-L's MI_Rb* / M_Rb*, else an M2-A fallback."""
	slots = list(meta.get("material_slots", []))
	out = {}
	all_b = slots and all(unreal.EditorAssetLibrary.does_asset_exist(f"{MATERIALS_B}/{s}") for s in slots)
	if all_b:
		bmat = _b_materials_module()
		if bmat is not None:
			try:
				paths = bmat.asset_materials(json_path)
				for slot, path in paths.items():
					out[slot] = (unreal.load_asset(path), "real")
			except Exception as error:  # noqa: BLE001
				_log(f"asset_materials({meta.get('asset_id')}) failed ({error}): per-slot lookup")
				out = {}
	for slot in slots:
		if slot in out and out[slot][0] is not None:
			continue
		found = None
		for folder in (MATERIALS_B, MATERIALS_L):
			path = f"{folder}/{slot}"
			if unreal.EditorAssetLibrary.does_asset_exist(path):
				found = (unreal.load_asset(path), "real")
				break
		if found is None:
			fb = fallback_material(slot)
			found = (fb, "fallback" if fb else "none")
		out[slot] = found
	return out


def resolve_material(slot: str):
	"""The material for one slot name: M2-B's MI_DB_*, M2-L's MI_Rb* / M_Rb*, else an M2-A fallback."""
	for folder in (MATERIALS_B, MATERIALS_L):
		path = f"{folder}/{slot}"
		if unreal.EditorAssetLibrary.does_asset_exist(path):
			return unreal.load_asset(path), "real"
	fb = fallback_material(slot)
	return fb, ("fallback" if fb else "none")


def normalize_meta(meta: dict) -> dict:
	"""One schema for the generators of M2-A (rb_bl: ue_folder / profile / target_m, venue axes) and M2-B (db_props_common: ue_mesh /
	ue_dir / collision_profile / target_dimensions_m, bounds in Blender axes with UE y = -Blender y)."""
	m = dict(meta)
	asset = m.get("asset_id", "?")
	if m.get("ue_mesh"):
		folder, name = m["ue_mesh"].rsplit("/", 1)
	else:
		folder = m.get("ue_dir") or f"{VENUE_ROOT}/{m.get('ue_folder', 'Props')}"
		name = m.get("mesh") or f"SM_DB_{asset}"
	m["_folder"], m["_name"] = folder, name
	m.setdefault("mesh", m.get("ledger_asset_id") or name)
	m["profile"] = m.get("profile") or m.get("collision_profile") or "RbVenueBlock"
	if "target_m" not in m and "target_dimensions_m" in m:
		m["target_m"] = m["target_dimensions_m"]
	m["_mirror_y"] = "-y" in str(m.get("axis_convention", "")).replace(" ", "")
	return m


def configure_mesh(mesh, meta: dict, json_path: str) -> list:
	"""Nanite / LODs, collision profile, physical material, materials by slot name. Returns report lines."""
	lines = []
	sms = _mesh_tools()
	translucent = bool(meta.get("translucent", False))
	nanite = mesh.get_editor_property("nanite_settings")
	nanite.set_editor_property("enabled", not translucent)
	for key, value in (("fallback_percent_triangles", 1.0), ("fallback_relative_error", 0.0)):
		try:
			nanite.set_editor_property(key, value)
		except Exception:  # noqa: BLE001 - property names differ between engine versions
			pass
	mesh.set_editor_property("nanite_settings", nanite)
	if translucent and meta.get("lods", True):
		options = unreal.EditorScriptingMeshReductionOptions()
		settings = []
		for pct, screen in ((1.0, 1.0), (0.5, 0.5), (0.25, 0.25), (0.12, 0.12)):
			s = unreal.EditorScriptingMeshReductionSettings()
			s.set_editor_property("percent_triangles", pct)
			s.set_editor_property("screen_size", screen)
			settings.append(s)
		options.set_editor_property("reduction_settings", settings)
		sms.set_lods(mesh, options)
	body = mesh.get_editor_property("body_setup")
	if body is not None:
		profile = meta.get("profile", "RbVenueBlock")
		try:
			body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX if meta.get("simple_as_complex") else unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
		except Exception:  # noqa: BLE001
			pass
		try:
			resp = body.get_editor_property("default_instance")
			resp.set_editor_property("collision_profile_name", profile)
			body.set_editor_property("default_instance", resp)
		except Exception:  # noqa: BLE001
			lines.append(f"  could not set the default collision profile {profile}")
		phys = meta.get("phys_material")
		if phys:
			path = f"{PHYS_DIR}/{phys}"
			if unreal.EditorAssetLibrary.does_asset_exist(path):
				body.set_editor_property("phys_material", unreal.load_asset(path))
				lines.append(f"  physical material {phys}")
			else:
				lines.append(f"  physical material {phys} not generated yet (M2-E) - default")
	if int(meta.get("collision_hulls", 0)) == 0:
		try:
			sms.remove_collisions(mesh)  # no UCX hulls: no generated box either (a re-import keeps the old body otherwise)
		except Exception:  # noqa: BLE001
			pass
	try:
		hulls = sms.get_simple_collision_count(mesh)
	except Exception:  # noqa: BLE001
		hulls = -1
	lines.append(f"  collision: {hulls} simple shape(s), profile {meta.get('profile', 'RbVenueBlock')}")
	if meta.get("collision_hulls", 0) and hulls == 0:
		lines.append("  WARNING: the FBX had UCX hulls but none were imported")
	resolved = resolve_materials(meta, json_path)
	slots = mesh.get_editor_property("static_materials")
	changed = False
	for i, sm in enumerate(slots):
		name = str(sm.get_editor_property("material_slot_name"))
		mat, kind = resolved.get(name) or resolve_material(name)
		if mat is not None:
			sm.set_editor_property("material_interface", mat)
			slots[i] = sm
			changed = True
		lines.append(f"  slot {name}: {kind}")
	if changed:
		mesh.set_editor_property("static_materials", slots)
	if meta.get("cast_shadow") is False:
		for i in range(len(slots)):
			try:
				sms.enable_section_cast_shadow(mesh, False, 0, i)
			except Exception:  # noqa: BLE001
				pass
	unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
	return lines


def bounds_check(mesh, meta: dict) -> list:
	"""VDB-T4 (importer half): the imported bounds reproduce the exporter's (1 mm) and the target dimensions (tolerance)."""
	box = mesh.get_bounding_box()
	lo = [box.min.x / 100.0, box.min.y / 100.0, box.min.z / 100.0]
	hi = [box.max.x / 100.0, box.max.y / 100.0, box.max.z / 100.0]
	want_lo, want_hi = list(meta["bounds_min_m"]), list(meta["bounds_max_m"])
	if meta.get("_mirror_y"):
		want_lo[1], want_hi[1] = -meta["bounds_max_m"][1], -meta["bounds_min_m"][1]
	problems = []
	for axis in range(3):
		for have, want, label in ((lo[axis], want_lo[axis], "min"), (hi[axis], want_hi[axis], "max")):
			if abs(have - want) > 0.001:
				problems.append(f"bounds {label} {'XYZ'[axis]}: UE {have:.4f} m, export {want:.4f} m")
	if "target_m" in meta:
		tol = float(meta.get("tolerance_m", 0.01))
		for axis in range(3):
			size = hi[axis] - lo[axis]
			if abs(size - meta["target_m"][axis]) > tol + 1e-6:
				problems.append(f"VDB-T4 {'XYZ'[axis]}: UE size {size:.4f} m, target {meta['target_m'][axis]:.4f} m (+-{tol})")
	return problems


def ledger_selftest() -> list:
	"""DB-0 negative test (host-usable): an asset without a ledger row and one with an NC licence must be refused, a listed own
	asset accepted. Returns the problems found per case."""
	ledger = {"SM_DB_Listed": {"asset_id": "SM_DB_Listed", "licence": "own"}, "T_NC": {"asset_id": "T_NC", "licence": "CC-BY-NC-4.0"}}
	cases = {
		"unlisted": ledger_check({"asset_id": "Unlisted", "mesh": "SM_DB_Unlisted"}, ledger),
		"nc_input": ledger_check({"asset_id": "Listed", "mesh": "SM_DB_Listed", "external_inputs": ["T_NC"]}, ledger),
		"listed": ledger_check({"asset_id": "Listed", "mesh": "SM_DB_Listed"}, ledger),
	}
	ok = bool(cases["unlisted"]) and bool(cases["nc_input"]) and not cases["listed"]
	_log(f"ledger self-test (DB-0 negative test): {'OK' if ok else 'FAILED'} {cases}")
	return [] if ok else [f"ledger self-test failed: {cases}"]


def main() -> None:
	argv = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	only, export_dir, check_only = [], EXPORT_DIR, False
	i = 0
	while i < len(argv):
		if argv[i] == "--only":
			i += 1
			while i < len(argv) and not argv[i].startswith("--"):
				only.append(argv[i])
				i += 1
			continue
		if argv[i] == "--export-dir" and i + 1 < len(argv):
			export_dir = argv[i + 1]
			i += 2
			continue
		if argv[i] == "--check-only":
			check_only = True
		i += 1
	problems = ledger_selftest()
	if problems:
		_fail(problems[0])
	ledger = load_ledger()
	assets = find_assets(export_dir)
	refused, imported = [], []
	for json_path in assets:
		with open(json_path, "r", encoding="utf-8") as handle:
			meta = normalize_meta(json.load(handle))
		asset = meta.get("asset_id", os.path.basename(os.path.dirname(json_path)))
		if only and asset not in only:
			continue
		problems = ledger_check(meta, ledger) + metadata_check(meta, json_path)
		if problems:
			refused.append(asset)
			for p in problems:
				_log(f"REFUSED {asset}: {p}")
			continue
		if check_only:
			_log(f"checked {asset}: OK")
			continue
		folder, name = meta["_folder"], meta["_name"]
		fbx = os.path.join(os.path.dirname(json_path), meta["fbx"])
		mesh = import_fbx(fbx, folder, name, build_nanite=not meta.get("translucent", False), collision=int(meta.get("collision_hulls", 0)) > 0)
		lines = configure_mesh(mesh, meta, json_path)
		problems = bounds_check(mesh, meta)
		for line in lines:
			_log(f"{asset}{line}")
		if problems:
			for p in problems:
				_log(f"{asset}: {p}")
			_fail(f"{asset}: imported bounds do not match (VDB-T4)")
		imported.append(asset)
		_log(f"imported {asset} -> {folder}/{name}")
	_log(f"rb_import_divebar: {len(imported)} imported, {len(refused)} refused, {len(assets)} asset metadata file(s) in {export_dir}")
	if refused:
		_fail(f"assets refused by the licence / metadata check: {refused}")


if __name__ == "__main__":
	main()
