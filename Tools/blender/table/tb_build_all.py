"""Table-body step of the Blender pipeline (runs INSIDE Blender through Tools/blender/divebar/db_build_all.py, or alone:
`python Tools/blender/rbbl.py run Tools/blender/table/tb_build_all.py`). Owner: M2-L. Docs/ue-architecture.md 18.7,
Docs/references/table-lookdev.md 3.

M2-L decision: every table body part (legs, levelers, apron, the coin-op cabinet with castings, trim, coin mechanism, trap window,
ball tray and cue-ball return) is generated in C++ by RbTableMeshBuilder from the same rb::TableGeometry as the playfield and baked
by Tools/unreal/editor/rb_bake_table.py, so no body is exported from Blender. This step keeps the pipeline contract of the plan:
it reads the `rbsim --geometry` exports in Art/Tables/ and asserts that they describe the tables the body is built for (the outer
cabinet 2.362 x 1.346 m of venue-dive-bar 3.1 for the 7-ft bar box, the 9-ft pro table's outer boundary), so a TableSpec change that
would move the cabinet away from the venue's spec fails here, where the venue generators run. Regenerate the exports with

  rbsim --table 7ft-bar --balls oldbar --geometry --no-trajectories --no-states --out Art/Tables/tablespec_seven_foot_bar.json
  rbsim --table 9ft-pro --geometry --no-trajectories --no-states --out Art/Tables/tablespec_nine_foot_pro.json
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "common"))
import rb_bl  # noqa: E402

REPO = HERE.parents[2]
TABLES = [
	# (export, TableSpec name, outer length / width [m] of the body (venue-dive-bar 3.1 for the bar box), tolerance [m])
	("tablespec_seven_foot_bar.json", "TABLE_7FT_BAR", 2.362, 1.346, 0.002),
	("tablespec_nine_foot_pro.json", "TABLE_9FT_PRO", 2.8956, 1.6256, 0.002),
]


def main() -> None:
	checked = []
	for filename, name, outer_l, outer_w, tol in TABLES:
		path = REPO / "Art" / "Tables" / filename
		if not path.exists():
			rb_bl.fail(f"{path} missing (rbsim --geometry export, see the docstring)")
		geometry = json.loads(path.read_text(encoding="utf-8")).get("geometry", {})
		if geometry.get("name") != name:
			rb_bl.fail(f"{filename}: table {geometry.get('name')} != {name}")
		length = geometry["length"] + 2.0 * geometry["railWidthTotal"]
		width = geometry["width"] + 2.0 * geometry["railWidthTotal"]
		if abs(length - outer_l) > tol or abs(width - outer_w) > tol:
			rb_bl.fail(f"{filename}: outer {length:.4f} x {width:.4f} m, the body is specified for {outer_l} x {outer_w} m (+-{tol} m)")
		if len(geometry.get("pockets", [])) != 6 or len(geometry.get("sights", [])) != 18:
			rb_bl.fail(f"{filename}: expected 6 pockets and 18 sights")
		checked.append(f"{name} {length:.4f} x {width:.4f} m")
	rb_bl.log("tb_build_all: table bodies are built by RbTableMeshBuilder (C++, rb_bake_table.py); exports checked: " + "; ".join(checked))


main()
