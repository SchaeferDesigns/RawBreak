"""Runs every dive-bar and table-body Blender generator in dependency order (runs INSIDE Blender; `python Tools/blender/rbbl.py all`).
Docs/ue-architecture.md 18.7 / 18.8, venue-dive-bar 13.1. Paths are relative to Tools/blender/.

Each generator runs in this Blender process with its own sys.argv (rb_bl.reset_scene() at its start); a missing generator is
skipped with a warning (--strict fails), a failing one stops the run. M2: the list was the architect's; M3 (Docs/ue-architecture.md
19.2): the file is M3-V's, which adds / removes only its own lines (the table and body lines belong to M3-L / M3-H). Owners in the
second column.
"""

from __future__ import annotations

import runpy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLENDER_DIR = HERE.parent
sys.path.insert(0, str(BLENDER_DIR / "common"))
import rb_bl  # noqa: E402

GENERATORS = [
	("divebar/db_axis_test.py", "M2-A"),      # DB-0: 1.000 m cube + arrow + "UP", pivot at the floor
	("divebar/db_arch.py", "M2-A"),           # shell from Art/DiveBar/layout.json (walls, ceiling, soffit, columns, doors, openings)
	("divebar/db_neon.py", "M2-A"),           # neon tubes from Art/DiveBar/neon/*.svg (lighting element; proxy lights in the level)
	("table/tb_build_all.py", "M2-L"),        # table bodies / coin-op cabinet from the rbsim --geometry JSONs in Art/Tables
	("body/bd_build_all.py", "M3-H"),         # M3 (Docs/ue-architecture.md 19.3): body cuffs / region-mask helpers (missing = skipped)
	("divebar/db_bar.py", "M2-B"),            # H08 bar counter
	("divebar/db_backbar.py", "M2-B"),        # H09 back bar, shelves, mirror, mug rack
	("divebar/db_booth.py", "M2-B"),          # M02 booths + booth tables
	("divebar/db_stool.py", "M2-B"),          # H10 bar stools (procedural, variants A / B / C) + spectator stools (M22)
	("divebar/db_ledges.py", "M2-B"),         # E15 column shelf, E16 back ledge, E17 left-wall ledge
	("divebar/db_lamp.py", "M2-B"),           # H03 3-shade table lamp (mesh; the bulbs are M2-A's lights)
	("divebar/db_cue_rack.py", "M2-B"),       # H05 wall cue rack + prop cues, H06 chalk cubes
	("divebar/db_jukebox.py", "M2-B"),        # H12 jukebox body
	("divebar/db_dart.py", "M2-B"),           # M03 dart machine + board
	("divebar/db_lathe_props.py", "M2-B"),    # C02 / H14 bottles, glasses (subset), C08 ashtray
	("divebar/db_signs.py", "M2-B"),          # wall signs and paper (S1 / S5 / S19 / S20, M15 / M25; text from Tools/art/text_textures.py)
	("divebar/db_decals.py", "M2-B"),         # first decal set (rings, burns, scuffs, stains) -> atlases
]


def main() -> None:
	strict = "--strict" in sys.argv
	passthrough = sys.argv[sys.argv.index("--"):] if "--" in sys.argv else ["--"]
	ran = []
	for script, owner in GENERATORS:
		path = BLENDER_DIR / script
		if not path.exists():
			if strict:
				rb_bl.fail(f"generator {script} ({owner}) missing")
			rb_bl.log(f"db_build_all: {script} ({owner}) not present yet - skipped")
			continue
		rb_bl.log(f"db_build_all: running {script} ({owner})")
		saved = sys.argv
		sys.argv = [saved[0], "--python", str(path)] + [a for a in passthrough if a != "--strict"]
		try:
			runpy.run_path(str(path), run_name="__main__")
		finally:
			sys.argv = saved
		ran.append(script)
	rb_bl.log(f"db_build_all: {len(ran)} generator(s) {ran}")


main()
