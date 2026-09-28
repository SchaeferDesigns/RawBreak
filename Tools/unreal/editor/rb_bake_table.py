"""UE-1: bakes the procedural table meshes (rb::TableGeometry -> RbTableMeshBuilder -> UStaticMesh) headless.

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_table.py                      # the committed presets
  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_bake_table.py NINE_FOOT_PRO ...     # explicit presets

Writes /Game/Generated/Tables/<Preset>/SM_Table_<Part> for every ERbTablePart (Docs/ue-architecture.md 5.3): Nanite with a
100 % fallback on every part but the thin sights (review R-02), complex-as-simple collision (pawn, cue sweeps), the part's
generated material when UE-3's materials exist. Idempotent: every run overwrites the assets from the current TableSpec, so
the assets are caches of code (pitfall 16). ARbTable loads them when present and falls back to the runtime dynamic meshes.
The C++ side already checks that each asset carries exactly the runtime mesh; this script re-checks what Python can see
(asset count, triangle counts > 0, bounds = the physics' outer boundary) and prints the metrics A2 compares.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

# M1 test room table + the dive-bar table (the two committed bakes; RawBreak.Unit.Table.BakedMatchesRuntime requires them).
DEFAULT_PRESETS = ["NINE_FOOT_PRO", "SEVEN_FOOT_BAR"]
PARTS = ["Bed", "CushionCloth", "RailCaps", "Apron", "PocketLiners", "Sights", "Legs"]
TABLE_DIR = "/Game/Generated/Tables"


def preset_dir_name(preset_enum_name: str) -> str:
	"""NINE_FOOT_PRO -> NineFootPro (the ERbTablePreset enumerator = RbTableMeshBuilder::GetPresetName)."""
	return "".join(word.capitalize() for word in preset_enum_name.split("_"))


def bake(preset_name: str) -> None:
	preset = getattr(unreal.RbTablePreset, preset_name, None)
	if preset is None:
		rb.fail(f"unknown table preset {preset_name}")
	written = unreal.RbAssetBakeLibrary.bake_table_meshes(preset, True)
	if written != len(PARTS):
		rb.fail(f"bake_table_meshes({preset_name}) wrote {written} assets, expected {len(PARTS)}")
	folder = f"{TABLE_DIR}/{preset_dir_name(preset_name)}"
	outer = None
	for part in PARTS:
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
		if part == "RailCaps":
			outer = box
	# The rail caps span the physics' OuterBoundary: symmetric about the bed centre.
	if outer is None or abs(outer.min.x + outer.max.x) > 1e-3 or abs(outer.min.y + outer.max.y) > 1e-3:
		rb.fail(f"{folder}: rail caps are not centred on the bed")


def main() -> None:
	args = [a for a in sys.argv[1:] if a and not a.lower().endswith(".py")]
	presets = [a.upper() for a in args] or DEFAULT_PRESETS
	rb.ensure_dir(TABLE_DIR)
	for preset_name in presets:
		bake(preset_name)
	rb.log(f"table bake OK: {', '.join(presets)}")


main()
