"""First-person player assets (M2-F; Docs/ue-architecture.md 18.3).

  python Tools/unreal/rbue.py py Tools/unreal/editor/rb_make_player.py

Creates under /Game/Generated/Player (RbAssetPaths::PlayerDir): SM_RbHand_Carry, the procedural stand-in hand that carries the
cue ball of the diegetic ball in hand (P2; replaced by the M3 arms / hands), and its material. Idempotent. Owner: M2-F.
STUB of the M2 architect step: TODO(M2-F).
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "unreal", "editor"))
import rb_common as rb  # noqa: E402


def main() -> None:
	rb.log("rb_make_player: not implemented yet (M2-F)")


main()
