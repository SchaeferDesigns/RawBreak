"""Table-body import step of rb_make_all.py (M2-L; Docs/ue-architecture.md 18.7, Docs/references/table-lookdev.md 3).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_import_table.py

M2-L builds every table body part in C++ (RbTableMeshBuilder: legs / levelers, apron, the coin-op cabinet, castings, trim,
hardware, trap window) from the same rb::TableGeometry as the playfield; rb_bake_table.py bakes them next to the playfield parts
(/Game/Generated/Tables/<Preset>/SM_Table_<Part>). Nothing is imported from Blender, so this step only keeps the contract of the
plan: it removes stale imports under /Game/Generated/Tables/<Preset>/Body/ (the folder the plan reserved for Blender bodies) and
checks that every baked body part the table's base style needs exists. Idempotent. Owner: M2-L.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

TABLE_DIR = "/Game/Generated/Tables"
# Body parts per preset folder (the base style's non-physics parts; RbTableMeshBuilder::PartExpected).
BODY_PARTS = {
	"NineFootPro": ["Apron", "Legs", "PocketBuckets", "Hardware", "RubberStrip"],
	"SevenFootBar": ["Apron", "Legs", "PocketBuckets", "Hardware", "RubberStrip", "Castings", "Cabinet", "Trim", "Window"],
}


def main() -> None:
	for preset, parts in BODY_PARTS.items():
		body_dir = f"{TABLE_DIR}/{preset}/Body"
		if unreal.EditorAssetLibrary.does_directory_exist(body_dir):
			if not unreal.EditorAssetLibrary.delete_directory(body_dir):
				rb.fail(f"could not remove the stale Blender import folder {body_dir}")
			rb.log(f"rb_import_table: removed stale {body_dir}")
		missing = [p for p in parts if not unreal.EditorAssetLibrary.does_asset_exist(f"{TABLE_DIR}/{preset}/SM_Table_{p}")]
		if missing:
			rb.fail(f"rb_import_table: {preset} body parts not baked: {', '.join(missing)} (run rb_bake_table.py)")
	rb.log("rb_import_table: table bodies are baked from C++ (rb_bake_table.py); nothing to import")


main()
