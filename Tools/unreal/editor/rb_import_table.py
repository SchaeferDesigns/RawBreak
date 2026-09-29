"""Imports the non-physics table bodies (M2-L; Docs/ue-architecture.md 18.7).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_import_table.py

Reads the exports of Tools/blender/table (Art/Tables/Export/<Preset>/<Asset>.fbx + <Asset>.json: the 9-ft apron / legs and the
7-ft coin-op cabinet of venue-dive-bar 3.1 - box, castings, legs, coin mechanism, trap window, ball tray, cue-ball return) and
imports them to /Game/Generated/Tables/<Preset>/Body/ (Nanite with the full-detail fallback, collision from the UCX hulls, table
material instances by slot name). Physics surfaces never come from here: the playfield parts are baked from rb::TableGeometry by
rb_bake_table.py. Refuses an asset without a ledger row when it uses external inputs. Idempotent. Owner: M2-L.
STUB of the M2 architect step: TODO(M2-L).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def main() -> None:
	rb.log("rb_import_table: not implemented yet (M2-L) - no table bodies imported")


main()
