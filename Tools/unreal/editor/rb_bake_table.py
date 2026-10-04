"""Bakes the procedural table meshes (rb::TableGeometry -> RbTableMeshBuilder -> UStaticMesh) headless. Owner: M2-L (UE-1 in M1).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_table.py                      # the committed presets
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_table.py NINE_FOOT_PRO ...     # explicit presets

Writes /Game/Generated/Tables/<Preset>/SM_Table_<Part> for every ERbTablePart the preset's base style builds
(Docs/ue-architecture.md 5.3, 18.7): the playfield parts (exact physics surfaces) and, since M2-L, the look-dev body - rubber
strips, drop pockets / gully throats, legs with levelers, and on the coin-op bar box (napped bar cloth, base style Cabinet) the
castings, the cabinet, the aluminium trim, the hardware and the trap window (venue-dive-bar 3.1). Nanite with a 100 % fallback on
every part but the thin sights (review R-02), complex-as-simple collision on every part, the part's default material
(RbTableMeshBuilder::GetDefaultMaterialPath: the dive-bar instances on the coin-op table) when rb_make_materials.py has generated
it. Parts the base style does not build are deleted (no stale assets of an earlier bake). Idempotent: every run overwrites the
assets from the current TableSpec, so the assets are caches of code (pitfall 16). ARbTable loads them when present and falls back
to the runtime dynamic meshes. The C++ side already checks that each asset carries exactly the runtime mesh; this script re-checks
what Python can see (asset count, triangle counts > 0, bounds) and prints the metrics A2 compares.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

# M1 test room table + the dive-bar table (the two committed bakes; RawBreak.Unit.Table.BakedMatchesRuntime requires them).
DEFAULT_PRESETS = ["NINE_FOOT_PRO", "SEVEN_FOOT_BAR"]
# ERbTablePart order (Core/RbTypes.h; the M2-L block appended after Legs).
PARTS = ["Bed", "CushionCloth", "RailCaps", "Apron", "PocketLiners", "Sights", "Legs", "RubberStrip", "PocketBuckets", "Castings", "Cabinet", "Trim",
	"Hardware", "Window"]
# Parts only the coin-op cabinet style builds (RbTableMeshBuilder::PartExpected).
CABINET_ONLY = {"Castings", "Cabinet", "Trim", "Window"}
# Presets with napped bar cloth resolve to the cabinet style (RbTableMeshBuilder::ResolveBaseStyle, BaseStyle Auto).
CABINET_PRESETS = {"SEVEN_FOOT_BAR", "SEVEN_FOOT_TRUE"}
TABLE_DIR = "/Game/Generated/Tables"


def preset_dir_name(preset_enum_name: str) -> str:
	"""NINE_FOOT_PRO -> NineFootPro (the ERbTablePreset enumerator = RbTableMeshBuilder::GetPresetName)."""
	return "".join(word.capitalize() for word in preset_enum_name.split("_"))


def expected_parts(preset_name: str) -> list:
	cabinet = preset_name in CABINET_PRESETS
	return [p for p in PARTS if cabinet or p not in CABINET_ONLY]


def bake(preset_name: str) -> None:
	preset = getattr(unreal.RbTablePreset, preset_name, None)
	if preset is None:
		rb.fail(f"unknown table preset {preset_name}")
	folder = f"{TABLE_DIR}/{preset_dir_name(preset_name)}"
	parts = expected_parts(preset_name)
	# Stale parts of the other base style (and the M1 Blender-body folder name reserved by rb_import_table.py) go first.
	for part in PARTS:
		if part not in parts:
			rb.delete_asset_if_exists(f"{folder}/SM_Table_{part}")
	written = unreal.RbAssetBakeLibrary.bake_table_meshes(preset, True)
	if written != len(parts):
		rb.fail(f"bake_table_meshes({preset_name}) wrote {written} assets, expected {len(parts)} ({', '.join(parts)})")
	outer = None
	for part in parts:
		path = f"{folder}/SM_Table_{part}"
		mesh = unreal.load_asset(path)
		if mesh is None or not isinstance(mesh, unreal.StaticMesh):
			rb.fail(f"{path} did not load as a StaticMesh")
		triangles = mesh.get_num_triangles(0)
		box = mesh.get_bounding_box()
		if triangles <= 0:
			rb.fail(f"{path} has no triangles")
		rb.log(f"{path}: {triangles} triangles, bounds min ({box.min.x:.4f}, {box.min.y:.4f}, {box.min.z:.4f}) "
			f"max ({box.max.x:.4f}, {box.max.y:.4f}, {box.max.z:.4f}) cm")
		if part == "Bed":
			outer = box
	# The bed (slate) is centred on the bed centre (the rails of the coin-op table are cut by castings, the bed never is).
	if outer is None or abs(outer.min.x + outer.max.x) > 1e-3 or abs(outer.min.y + outer.max.y) > 1e-3:
		rb.fail(f"{folder}: the bed is not centred on the table origin")


def main() -> None:
	args = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	presets = [a.upper() for a in args] or DEFAULT_PRESETS
	rb.ensure_dir(TABLE_DIR)
	for preset_name in presets:
		bake(preset_name)
	rb.log(f"table bake OK: {', '.join(presets)}")


main()
