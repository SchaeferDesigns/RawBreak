"""The minimal title / venue-select level /Game/Generated/Maps/L_Title (M2-D; Docs/ue-architecture.md 18.4).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_title.py

A dark backdrop with one ARbLookDevCamera (Eyes) and World Settings GameMode ARbTitleGameMode, which shows SRbTitleScreen:
RAW BREAK - Play: Test room / Dive bar (x Practice / Hot-seat; an entry whose level does not exist yet is disabled with the
reason), Settings, Quit. The 3D main menu "Closing Time" in the dive bar's AfterHours state replaces it later (ui-ux 6).
Idempotent. Owner: M2-D. STUB of the M2 architect step: TODO(M2-D).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402

MAP = "/Game/Generated/Maps/L_Title"  # RbAssetPaths::TitleMap


def main() -> None:
	rb.log(f"rb_make_title: not implemented yet (M2-D) - {MAP} not generated")


main()
