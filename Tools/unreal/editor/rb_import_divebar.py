"""Imports the Blender exports of a venue into Unreal (M2-A pipeline, used by M2-A / M2-B / M2-L; venue-dive-bar 13.8).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_import_divebar.py [-- --only <AssetId> ...]

Scans Art/DiveBar/Export/<Asset>/<Asset>.json (one metadata file per asset, written by the Blender generators through
Tools/blender/common/rb_bl.py; NO shared manifest, so generators of different packages never edit the same file) and for each:
  * refuses the asset without a licence-ledger row for every external input it lists (Docs/licenses/asset-ledger.csv or a
    package fragment Docs/licenses/ledger/<package>.csv) - the DB-0 negative test;
  * imports the FBX with unreal.AssetImportTask (automated, replace existing; Interchange, materials off) to
    /Game/Generated/Venues/DiveBar/<Family>/SM_DB_<...>, textures from Art/DiveBar/Textures/<Asset>/ (BC7 / BC5 / linear masks,
    VT streaming >= 2048);
  * sets Nanite (full-detail fallback, ARCH 2.2) for opaque meshes, 3 auto LODs for translucent ones, UCX collision (no auto
    collision), the collision profile named in the metadata, ray-tracing visibility, and the MI_DB_* / MI_Rb* material of each
    slot name;
  * re-checks the bounds against the metadata's target dimensions (VDB-T4: +-2 mm hero, +-1 cm others).
Idempotent. Owner: M2-A. STUB of the M2 architect step: TODO(M2-A).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

EXPORT_DIR = os.path.join(unreal.Paths.project_dir(), "Art", "DiveBar", "Export")


def main() -> None:
	count = 0
	if os.path.isdir(EXPORT_DIR):
		count = sum(1 for name in os.listdir(EXPORT_DIR) if os.path.isfile(os.path.join(EXPORT_DIR, name, name + ".json")))
	rb.log(f"rb_import_divebar: not implemented yet (M2-A) - {count} exported asset(s) found, nothing imported")


main()
